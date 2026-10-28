#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>

class Scene;

// Render de la escena: shaders, rejilla, textura checker y el framebuffer donde se dibuja (el
// editor lo muestra en su panel Viewport). No sabe nada del editor: recibe la escena y las
// matrices de la camara y dibuja.
class Renderer
{
public:
    static constexpr float NORMAL_LENGTH_PERCENT = 0.03f;   // % de la diagonal del AABB local de la malla

    // Ajustes de render (los cambian Configuration, el menu View y la tecla N).
    glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    bool frustumCulling = true;
    bool showNormals = false;       // normales por vertice de los objetos en modo "Use Global"
    bool showFaceNormals = false;   // normales por cara de todos los objetos

    // Necesita el contexto de OpenGL ya creado.
    void Init(int viewportWidth, int viewportHeight);
    void Shutdown();

    // Redimensiona el framebuffer del viewport (ancho y alto >= 1).
    void ResizeViewport(int width, int height);

    // Dibuja la rejilla y los objetos visibles en el framebuffer del viewport, con frustum culling
    // y normales de debug. Devuelve cuantos objetos se han dibujado.
    int Render(const Scene& scene, const glm::mat4& view, const glm::mat4& projection);

    GLuint GetCheckerTexture() const { return checkerTexture; }
    GLuint GetViewportTexture() const { return viewportTexture; }
    int GetViewportWidth() const { return viewportWidth; }
    int GetViewportHeight() const { return viewportHeight; }

private:
    void CreateViewportFramebuffer();
    void CreateCheckerTexture();
    void CreateGrid();

    GLuint shaderProgram = 0, normalShaderProgram = 0, gridShaderProgram = 0;
    GLint uModelLoc = -1, uViewLoc = -1, uProjLoc = -1;
    GLint normalUModelLoc = -1, normalUViewLoc = -1, normalUProjLoc = -1;
    GLint normalULengthLoc = -1, normalUColorLoc = -1;
    GLint gridUViewLoc = -1, gridUProjLoc = -1;

    GLuint viewportFBO = 0, viewportTexture = 0, viewportRBO = 0;
    int viewportWidth = 1280, viewportHeight = 720;

    GLuint checkerTexture = 0;

    GLuint gridVAO = 0, gridVBO = 0;
    GLsizei gridVertexCount = 0;
};
