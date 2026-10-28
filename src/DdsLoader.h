#pragma once
#include <glad/glad.h>

// Carga un .dds como textura 2D de OpenGL. Soporta DXT1/DXT3/DXT5 (comprimidos, con los mipmaps del
// archivo) y RGB/RGBA sin comprimir de 24/32 bits. Los datos se voltean verticalmente para
// coincidir con las PNG cargadas por stb_image (origen abajo a la izquierda).
// Devuelve 0 si falla; el motivo se escribe en la Consola.
GLuint LoadDDSTexture(const char* path, int* outWidth = nullptr, int* outHeight = nullptr);
