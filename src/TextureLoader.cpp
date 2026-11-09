#include "TextureLoader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "DdsLoader.h"
#include "Logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

bool ReadWholeFile(const std::string& utf8Path, std::vector<uint8_t>& out)
{
    const std::filesystem::path path(std::u8string(utf8Path.begin(), utf8Path.end()));
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// flipVertically: el volteo en Y para OpenGL (origen abajo); el flip Y del usuario lo deshace.
static bool DecodeRaw(const std::string& utf8Path, TextureData& out, bool flipVertically)
{
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(utf8Path, bytes))
    {
        Console::LogError("Textura: no se pudo abrir %s", utf8Path.c_str());
        return false;
    }

    std::string lower = utf8Path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.ends_with(".dds"))
        return DecodeDDS(bytes, utf8Path, out, flipVertically);

    // Se fuerza RGBA (4 canales) porque una imagen de 1 o 2 canales (grises), o de 3 con ancho no
    // multiplo de 4, se subiria con un formato o una alineacion que no le corresponde.
    stbi_set_flip_vertically_on_load(flipVertically);
    int w = 0, h = 0, channels = 0;
    unsigned char* img = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
    if (!img)
    {
        Console::LogError("stb_image: no se pudo cargar la textura %s (%s)", utf8Path.c_str(), stbi_failure_reason());
        return false;
    }
    out.format = TextureData::Format::RGBA8;
    out.width = uint32_t(w);
    out.height = uint32_t(h);
    out.mips.assign(1, std::vector<uint8_t>(img, img + size_t(w) * h * 4));
    stbi_image_free(img);

    Console::LogInfo("stb_image: textura decodificada %s (%dx%d, %d canales)", utf8Path.c_str(), w, h, channels);
    return true;
}

// Voltea en horizontal un nivel RGBA8, en el sitio.
static void FlipRGBA8X(std::vector<uint8_t>& px, uint32_t w, uint32_t h)
{
    const size_t row = size_t(w) * 4;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w / 2; ++x)
        {
            const auto a = px.begin() + y * row + x * 4;
            std::swap_ranges(a, a + 4, px.begin() + y * row + (w - 1 - x) * 4);
        }
}

bool DecodeImageFile(const std::string& utf8Path, TextureData& out, const TextureImportOptions& options)
{
    // El flip Y se hace al decodificar: es no voltear (stb_image y DDS ya voltean para OpenGL).
    if (!DecodeRaw(utf8Path, out, !options.flipY)) return false;
    out.sampler = options.sampler;
    if (!options.flipX) return true;

    // En horizontal, los bloques DXT habria que reordenarlos por dentro: no compensa.
    if (out.format != TextureData::Format::RGBA8)
    {
        Console::LogInfo("Textura: el flip X no se aplica a los DDS comprimidos (DXT): %s", utf8Path.c_str());
        return true;
    }
    uint32_t w = out.width, h = out.height;
    for (auto& mip : out.mips)
    {
        FlipRGBA8X(mip, w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return true;
}

void ApplySampler(GLuint texture, const TextureSampler& sampler)
{
    const auto wrap = [](TextureWrap w) {
        return GLint(w == TextureWrap::Clamp ? GL_CLAMP_TO_EDGE : w == TextureWrap::Mirrored ? GL_MIRRORED_REPEAT : GL_REPEAT);
    };
    GLint minF = GL_LINEAR_MIPMAP_LINEAR, magF = GL_LINEAR;
    if (sampler.filter == TextureFilter::Linear) minF = GL_LINEAR;
    else if (sampler.filter == TextureFilter::Nearest) minF = magF = GL_NEAREST;
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, minF);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, magF);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, wrap(sampler.wrapS));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, wrap(sampler.wrapT));
}

GLuint UploadTexture(const TextureData& data)
{
    if (data.mips.empty() || data.width == 0 || data.height == 0) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GLenum compressedFormat = 0;
    switch (data.format)
    {
    case TextureData::Format::DXT1: compressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; break;
    case TextureData::Format::DXT3: compressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; break;
    case TextureData::Format::DXT5: compressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; break;
    default: break;
    }

    uint32_t w = data.width, h = data.height;
    for (size_t level = 0; level < data.mips.size(); ++level)
    {
        const std::vector<uint8_t>& mip = data.mips[level];
        if (compressedFormat)
            glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), compressedFormat, GLsizei(w), GLsizei(h), 0, GLsizei(mip.size()), mip.data());
        else
            glTexImage2D(GL_TEXTURE_2D, GLint(level), GL_RGBA, GLsizei(w), GLsizei(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, mip.data());
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // Sin comprimir y con un solo nivel: se generan los mipmaps. Si no, MAX_LEVEL deja la textura
    // completa con los niveles que hay (un filtro con mipmaps no pide niveles que no existen).
    if (!compressedFormat && data.mips.size() == 1)
        glGenerateMipmap(GL_TEXTURE_2D);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(data.mips.size() - 1));
    glBindTexture(GL_TEXTURE_2D, 0);
    ApplySampler(tex, data.sampler);
    return tex;
}
