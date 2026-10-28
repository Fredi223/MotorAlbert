#pragma once
#include <glad/glad.h>
#include <string>

// Sube una imagen (PNG, JPG, TGA...) a una textura RGBA con mipmaps usando stb_image. La imagen se
// voltea verticalmente al cargarla (origen abajo a la izquierda, como espera OpenGL).
// Devuelve 0 si stb_image no puede leerla; el motivo se escribe en la Consola.
GLuint LoadImageTexture(const std::string& path);
