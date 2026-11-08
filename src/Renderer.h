#pragma once
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

class Scene;

// Lo que ha hecho el ultimo Render (lo muestran el Viewport y Configuration).
struct RenderStats
{
    int drawn = 0;            // objetos dibujados
    int culled = 0;           // objetos con malla y no ocultos que el frustum culling ha descartado
    int candidates = 0;       // objetos que ha devuelto la consulta del octree
    int nodesVisited = 0;     // nodos del octree que ha mirado la consulta...
    int nodesTotal = 0;       // ...de todos los que tiene
    std::string cullingCamera;   // nombre de la camara de juego que hace el culling; vacio = la del editor
};

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

    // Debug draw (lineas)
    bool showAABBs = false;            // AABB de mundo de cada objeto dibujado
    bool showOctree = false;           // cajas de los nodos del octree
    bool showPickRay = false;          // ultimo rayo de picking
    bool showCameraFrustums = true;    // frustum de las camaras de juego

    // Necesita el contexto de OpenGL ya creado.
    void Init(int viewportWidth, int viewportHeight);
    void Shutdown();

    // Redimensiona el framebuffer del viewport (ancho y alto >= 1).
    void ResizeViewport(int width, int height);

    // Dibuja la rejilla, los objetos que pasan el frustum culling (consultando el octree) y el debug
    // draw en el framebuffer del viewport. view/projection son los de la camara desde la que se mira;
    // el culling usa la culling camera de la escena si hay una, o esta misma camara si no.
    RenderStats Render(const Scene& scene, const glm::mat4& view, const glm::mat4& projection);

    // Ultimo rayo de picking (en mundo), para el debug draw.
    void SetPickRay(const glm::vec3& from, const glm::vec3& to) { pickRayFrom = from; pickRayTo = to; hasPickRay = true; }

    GLuint GetCheckerTexture() const { return checkerTexture; }
    GLuint GetViewportTexture() const { return viewportTexture; }
    int GetViewportWidth() const { return viewportWidth; }
    int GetViewportHeight() const { return viewportHeight; }

private:
    void CreateViewportFramebuffer();
    void CreateCheckerTexture();
    void CreateGrid();

    // Lineas de debug: se acumulan durante el frame y se dibujan todas juntas en FlushLines.
    void AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color);
    void AddBox(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color);
    void AddFrustum(const glm::mat4& vp, const glm::vec3& color);
    void FlushLines(const glm::mat4& viewProj);

    GLuint shaderProgram = 0, normalShaderProgram = 0, gridShaderProgram = 0, lineShaderProgram = 0;
    GLint uModelLoc = -1, uViewLoc = -1, uProjLoc = -1;
    GLint normalUModelLoc = -1, normalUViewLoc = -1, normalUProjLoc = -1;
    GLint normalULengthLoc = -1, normalUColorLoc = -1;
    GLint gridUViewLoc = -1, gridUProjLoc = -1;
    GLint lineUViewProjLoc = -1;

    GLuint viewportFBO = 0, viewportTexture = 0, viewportRBO = 0;
    int viewportWidth = 1280, viewportHeight = 720;

    GLuint checkerTexture = 0;

    GLuint gridVAO = 0, gridVBO = 0;
    GLsizei gridVertexCount = 0;

    GLuint lineVAO = 0, lineVBO = 0;
    std::vector<float> lineVertices;   // 6 floats por vertice: posicion + color

    bool hasPickRay = false;
    glm::vec3 pickRayFrom{ 0.0f }, pickRayTo{ 0.0f };
};
