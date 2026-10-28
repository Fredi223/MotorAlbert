#include "DdsLoader.h"
#include "Logger.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

namespace
{
    constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
    {
        return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
    }

    constexpr uint32_t DDS_MAGIC = MakeFourCC('D', 'D', 'S', ' ');
    constexpr uint32_t FOURCC_DXT1 = MakeFourCC('D', 'X', 'T', '1');
    constexpr uint32_t FOURCC_DXT3 = MakeFourCC('D', 'X', 'T', '3');
    constexpr uint32_t FOURCC_DXT5 = MakeFourCC('D', 'X', 'T', '5');
    constexpr uint32_t DDPF_ALPHAPIXELS = 0x1;
    constexpr uint32_t DDPF_FOURCC = 0x4;
    constexpr uint32_t DDPF_RGB = 0x40;
    constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;

    struct DdsHeader   // 124 bytes, tras los 4 del magic
    {
        uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
        uint32_t reserved1[11];
        uint32_t pfSize, pfFlags, pfFourCC, pfRGBBitCount, pfRMask, pfGMask, pfBMask, pfAMask;
        uint32_t caps, caps2, caps3, caps4, reserved2;
    };
    static_assert(sizeof(DdsHeader) == 124, "DdsHeader debe medir 124 bytes");

    // Voltea las filas de un bloque de color DXT (4 bytes de colores + 1 byte de indices por fila).
    // validRows < 4 en los ultimos mips (alto 1 o 2): solo esas filas existen y se intercambian entre si.
    void FlipColorBlock(uint8_t* b, int validRows)
    {
        uint8_t* rows = b + 4;
        uint8_t tmp[4];
        std::memcpy(tmp, rows, 4);
        for (int r = 0; r < validRows; ++r) rows[r] = tmp[validRows - 1 - r];
    }

    // Alfa DXT3: 2 bytes (4 nibbles) por fila.
    void FlipDxt3Alpha(uint8_t* a, int validRows)
    {
        uint8_t tmp[8];
        std::memcpy(tmp, a, 8);
        for (int r = 0; r < validRows; ++r) std::memcpy(a + r * 2, tmp + (validRows - 1 - r) * 2, 2);
    }

    // Alfa DXT5: 2 bytes de extremos + 48 bits de indices de 3 bits (12 bits por fila).
    void FlipDxt5Alpha(uint8_t* a, int validRows)
    {
        uint64_t bits = 0;
        for (int i = 0; i < 6; ++i) bits |= uint64_t(a[2 + i]) << (8 * i);
        uint64_t out = 0;
        for (int r = 0; r < validRows; ++r)
        {
            const uint64_t row = (bits >> (12 * (validRows - 1 - r))) & 0xFFF;
            out |= row << (12 * r);
        }
        for (int i = 0; i < 6; ++i) a[2 + i] = uint8_t(out >> (8 * i));
    }

    // Voltea verticalmente un mip comprimido: invierte el orden de las filas de bloques y, dentro de
    // cada bloque, el de sus 4 filas de texels.
    void FlipCompressedLevel(std::vector<uint8_t>& data, uint32_t width, uint32_t height, uint32_t fourCC)
    {
        const size_t blockBytes = fourCC == FOURCC_DXT1 ? 8 : 16;
        const size_t blocksX = std::max<uint32_t>(1, (width + 3) / 4);
        const size_t blocksY = std::max<uint32_t>(1, (height + 3) / 4);
        const int validRows = height >= 4 ? 4 : int(height);
        const size_t rowBytes = blocksX * blockBytes;

        for (size_t y = 0; y < blocksY / 2; ++y)
            std::swap_ranges(data.begin() + y * rowBytes, data.begin() + (y + 1) * rowBytes,
                data.begin() + (blocksY - 1 - y) * rowBytes);

        for (size_t i = 0; i < blocksX * blocksY; ++i)
        {
            uint8_t* block = data.data() + i * blockBytes;
            if (fourCC == FOURCC_DXT1)
            {
                FlipColorBlock(block, validRows);
            }
            else
            {
                if (fourCC == FOURCC_DXT3) FlipDxt3Alpha(block, validRows);
                else FlipDxt5Alpha(block, validRows);
                FlipColorBlock(block + 8, validRows);
            }
        }
    }

    // Extrae un canal de un pixel sin comprimir segun su mascara, reescalado a 0..255.
    uint8_t ExtractChannel(uint32_t pixel, uint32_t mask)
    {
        if (mask == 0) return 255;
        int shift = 0;
        while (((mask >> shift) & 1u) == 0) ++shift;
        const uint32_t max = mask >> shift;
        return uint8_t(((pixel & mask) >> shift) * 255u / max);
    }
}

GLuint LoadDDSTexture(const char* path, int* outWidth, int* outHeight)
{
    FILE* f = std::fopen(path, "rb");
    if (!f)
    {
        Console::LogError("DDS: no se pudo abrir %s", path);
        return 0;
    }

    uint32_t magic = 0;
    DdsHeader h{};
    if (std::fread(&magic, 4, 1, f) != 1 || magic != DDS_MAGIC || std::fread(&h, sizeof(h), 1, f) != 1 || h.size != 124)
    {
        std::fclose(f);
        Console::LogError("DDS: %s no es un DDS valido", path);
        return 0;
    }

    const bool compressed = (h.pfFlags & DDPF_FOURCC) != 0;
    GLenum format = 0;
    if (compressed)
    {
        if (h.pfFourCC == FOURCC_DXT1) format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        else if (h.pfFourCC == FOURCC_DXT3) format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        else if (h.pfFourCC == FOURCC_DXT5) format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    }
    const bool uncompressedOk = !compressed && (h.pfFlags & DDPF_RGB) && (h.pfRGBBitCount == 32 || h.pfRGBBitCount == 24);
    if (!format && !uncompressedOk)
    {
        std::fclose(f);
        Console::LogError("DDS: formato no soportado en %s (solo DXT1/DXT3/DXT5 y RGB/RGBA sin comprimir)", path);
        return 0;
    }
    if (h.width == 0 || h.height == 0 || h.width > 16384 || h.height > 16384)
    {
        std::fclose(f);
        Console::LogError("DDS: dimensiones invalidas en %s (%ux%u)", path, h.width, h.height);
        return 0;
    }

    const uint32_t mipCount = (h.flags & DDSD_MIPMAPCOUNT) ? std::clamp<uint32_t>(h.mipMapCount, 1, 15) : 1;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    uint32_t uploaded = 0;
    uint32_t w = h.width, ht = h.height;
    for (uint32_t level = 0; level < mipCount; ++level)
    {
        if (compressed)
        {
            const size_t blockBytes = h.pfFourCC == FOURCC_DXT1 ? 8 : 16;
            const size_t size = size_t(std::max<uint32_t>(1, (w + 3) / 4)) * std::max<uint32_t>(1, (ht + 3) / 4) * blockBytes;
            std::vector<uint8_t> data(size);
            if (std::fread(data.data(), 1, size, f) != size) break;
            FlipCompressedLevel(data, w, ht, h.pfFourCC);
            glCompressedTexImage2D(GL_TEXTURE_2D, GLint(level), format, GLsizei(w), GLsizei(ht), 0, GLsizei(size), data.data());
        }
        else
        {
            const uint32_t bytesPerPixel = h.pfRGBBitCount / 8;
            std::vector<uint8_t> src(size_t(w) * ht * bytesPerPixel);
            if (std::fread(src.data(), 1, src.size(), f) != src.size()) break;
            const bool hasAlpha = (h.pfFlags & DDPF_ALPHAPIXELS) != 0;
            std::vector<uint8_t> rgba(size_t(w) * ht * 4);
            for (uint32_t y = 0; y < ht; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    uint32_t px = 0;
                    std::memcpy(&px, &src[(size_t(y) * w + x) * bytesPerPixel], bytesPerPixel);
                    uint8_t* dst = &rgba[(size_t(ht - 1 - y) * w + x) * 4];   // fila volteada
                    dst[0] = ExtractChannel(px, h.pfRMask);
                    dst[1] = ExtractChannel(px, h.pfGMask);
                    dst[2] = ExtractChannel(px, h.pfBMask);
                    dst[3] = hasAlpha ? ExtractChannel(px, h.pfAMask) : 255;
                }
            }
            glTexImage2D(GL_TEXTURE_2D, GLint(level), GL_RGBA, GLsizei(w), GLsizei(ht), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        ++uploaded;
        w = std::max(1u, w / 2);
        ht = std::max(1u, ht / 2);
    }
    std::fclose(f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (uploaded == 0)
    {
        glDeleteTextures(1, &tex);
        Console::LogError("DDS: %s esta truncado", path);
        return 0;
    }

    // Sin comprimir y sin mips en el archivo: se generan. Si no, MAX_LEVEL deja la textura completa
    // con los niveles que trae el archivo (un filtro con mipmaps no pide niveles que no existen).
    if (!compressed && uploaded == 1)
        glGenerateMipmap(GL_TEXTURE_2D);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(uploaded - 1));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (outWidth) *outWidth = int(h.width);
    if (outHeight) *outHeight = int(h.height);
    const char* kind = !compressed ? "RGBA sin comprimir" : h.pfFourCC == FOURCC_DXT1 ? "DXT1" : h.pfFourCC == FOURCC_DXT3 ? "DXT3" : "DXT5";
    Console::LogInfo("DDS: textura cargada %s (%ux%u, %s, %u mips)", path, h.width, h.height, kind, uploaded);
    return tex;
}
