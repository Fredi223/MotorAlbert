#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>

// Estado del sampler de una textura. Es una opcion de importado (va en el .meta) y se guarda tambien
// en el .tex, para aplicarlo al cargar sin leer el .meta.
enum class TextureFilter : uint32_t { LinearMipmaps = 0, Linear = 1, Nearest = 2 };
enum class TextureWrap : uint32_t { Repeat = 0, Clamp = 1, Mirrored = 2 };

struct TextureSampler
{
    TextureFilter filter = TextureFilter::LinearMipmaps;
    TextureWrap wrapS = TextureWrap::Repeat;
    TextureWrap wrapT = TextureWrap::Repeat;
    bool operator==(const TextureSampler&) const = default;
};

// Opciones de importado de una textura (las edita el Inspector del asset). El flip cambia los pixeles,
// asi que cambiarlo obliga a reimportar; los DDS comprimidos (DXT) solo se voltean en Y.
struct TextureImportOptions
{
    TextureSampler sampler;
    bool flipX = false;
    bool flipY = false;
    bool operator==(const TextureImportOptions&) const = default;
};

// Textura decodificada en memoria de CPU, lista para guardarla en Library (.tex) o subirla a la GPU.
// Las filas ya estan volteadas (origen abajo a la izquierda, como espera OpenGL).
struct TextureData
{
    enum class Format : uint32_t { RGBA8 = 0, DXT1 = 1, DXT3 = 2, DXT5 = 3 };

    Format format = Format::RGBA8;
    uint32_t width = 0, height = 0;
    // Nivel 0 primero. Un RGBA8 con un solo nivel recibe los mipmaps al subirlo (glGenerateMipmap);
    // los DDS traen los suyos.
    std::vector<std::vector<uint8_t>> mips;
    TextureSampler sampler;
};

// Decodifica una imagen segun su extension: PNG/JPG/TGA/BMP con stb_image (siempre a RGBA) y DDS con
// DdsLoader. Aplica los flips de las opciones (no a los DXT) y copia su sampler. false si falla; el
// motivo se escribe en la Consola.
bool DecodeImageFile(const std::string& utf8Path, TextureData& out, const TextureImportOptions& options = {});

// Crea una textura de OpenGL con los datos y su sampler (siempre con mipmaps). 0 si no hay datos.
GLuint UploadTexture(const TextureData& data);

// Aplica filtro y wrap a una textura ya creada.
void ApplySampler(GLuint texture, const TextureSampler& sampler);

// Lee un archivo entero a memoria. La ruta es UTF-8 (asi funciona con tildes en Windows, donde
// fopen interpreta las rutas en la codificacion ANSI).
bool ReadWholeFile(const std::string& utf8Path, std::vector<uint8_t>& out);

// Textura ya subida a la GPU (la crea el ResourceManager). La comparten (TexturePtr) los materiales
// que la usan y los snapshots del undo; cuando la suelta el ultimo, se borra de la GPU.
struct Texture
{
    uint64_t uid = 0;
    GLuint id = 0;
    uint32_t width = 0, height = 0;
    TextureData::Format format = TextureData::Format::RGBA8;
};
using TexturePtr = std::shared_ptr<Texture>;
