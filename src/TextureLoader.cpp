#include "TextureLoader.h"
#include "Logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Se fuerza RGBA (4 canales) porque una imagen de 1 o 2 canales (grises), o de 3 con ancho no
// multiplo de 4, se subiria con un formato o una alineacion que no le corresponde.
GLuint LoadImageTexture(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true);

    int w, h, channels;
    unsigned char* img = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!img)
    {
        Console::LogError("stb_image: no se pudo cargar la textura %s (%s)", path.c_str(), stbi_failure_reason());
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(img);

    Console::LogInfo("stb_image: textura cargada %s (%dx%d, %d canales)", path.c_str(), w, h, channels);
    return tex;
}
