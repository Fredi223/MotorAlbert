#pragma once
#include <glad/glad.h>

// Fuentes GLSL del motor (definidos en Shaders.cpp).
extern const char* vertexShaderSource;        // modelos: textura + luz difusa
extern const char* fragmentShaderSource;
extern const char* normalVertexShaderSrc;     // visualizador de normales (con geometry shader)
extern const char* normalGeometryShaderSrc;
extern const char* normalFragmentShaderSrc;
extern const char* gridVertexShaderSrc;       // rejilla de referencia
extern const char* gridFragmentShaderSrc;

// Compila y enlaza un shader program. geometrySrc es opcional (solo lo usa el shader de normales).
// Los errores de compilacion o enlazado se escriben en la Consola.
GLuint CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc, const char* geometrySrc = nullptr);
