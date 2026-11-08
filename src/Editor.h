#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Camera.h"
#include "Renderer.h"   // RenderStats

class FrameTiming;
class Renderer;
class ResourceManager;
class Scene;
class UndoHistory;
struct GameObject;

// Editor del motor: la interfaz de ImGui (menu, paneles, viewport), la camara del editor, la
// seleccion y el input. Usa el motor (Scene, Renderer, FrameTiming) pero el motor no sabe nada de el.
//
// Los metodos estan repartidos en varios .cpp segun lo que dibujan:
//   Editor.cpp             Init/Shutdown, eventos, orden del frame, menu y dockspace
//   EditorCamera.cpp       input de la camara libre/orbital, captura del raton, encuadre (F)
//   PanelInspector.cpp     Inspector
//   PanelConfiguration.cpp Configuration
//   PanelResources.cpp     Resources (recursos importados y sus referencias)
//   PanelHierarchy.cpp     Hierarchy y popup de renombrar
//   PanelViewport.cpp      Viewport y picking
class Editor
{
public:
    Editor(SDL_Window* window, Scene& scene, ResourceManager& resources, UndoHistory& history, Renderer& renderer, FrameTiming& timing);

    // Crea el contexto de ImGui y toma el estado actual de la escena como inicio del undo.
    void Init(SDL_GLContext context);
    void Shutdown();

    void ProcessEvent(const SDL_Event& e);

    // Construye y dibuja un frame entero del editor (incluido el render de la escena en el viewport).
    void DrawFrame();

    // Escape o File > Exit.
    bool WantsToQuit() const { return quitRequested; }

    // Payload de drag & drop de un recurso (uint64_t con su UID): lo arrastran los paneles Resources y
    // Assets y lo aceptan los campos Mesh y Texture del Inspector.
    static constexpr const char* DND_RESOURCE = "DND_RESOURCE";

    // FOV, near y far de una camara (la del editor en Configuration y las de juego en el Inspector).
    static void DrawLensFields(ComponentCamera& lens);

private:
    // --- Editor.cpp ---
    void DrawMainMenuBar();
    void DrawDockspace();
    void ImportDroppedFile(const std::string& filePath);
    void AssignDroppedTexture(uint64_t textureUid);

    // --- EditorCamera.cpp ---
    void UpdateCamera();
    void BeginMouseCapture();
    void EndMouseCapture();
    void ReadMouseDelta(float& dx, float& dy);
    void FrameSelectedObject();

    // --- Paneles ---
    void DrawInspector();
    void DrawConfiguration();
    void DrawResources();
    void DrawHierarchy();
    void DrawViewport();

    // --- PanelViewport.cpp ---
    // Objeto con malla mas cercano que atraviesa el rayo (test contra sus triangulos), o nullptr.
    GameObject* PickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDir) const;

    // --- PanelHierarchy.cpp ---
    // Lo que se pide al dibujar el arbol se aplica al terminar de recorrerlo (borrar o mover
    // objetos en mitad del recorrido invalidaria los iteradores de children).
    struct HierarchyOps {
        GameObject* toDelete = nullptr;
        GameObject* toDuplicate = nullptr;
        GameObject* createChildOf = nullptr;
        uint64_t reparentUid = 0;           // 0 = nada que reparentar
        GameObject* reparentTo = nullptr;   // nullptr = primer nivel
    };
    void DrawHierarchyNode(GameObject& go, HierarchyOps& ops);
    void BeginRename(const GameObject& go);
    // Borra el objeto y su rama, soltando antes la seleccion si estaba dentro.
    void DeleteObject(GameObject* go);

    // Motor
    SDL_Window* window;
    Scene& scene;
    ResourceManager& resources;
    UndoHistory& history;
    Renderer& renderer;
    FrameTiming& timing;

    bool quitRequested = false;

    // Camara del editor y sus matrices del frame actual
    Camera camera;
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    // Seleccion. Las direcciones de los GameObject son estables; quien borra o sustituye objetos
    // (DeleteObject, el undo) la actualiza.
    GameObject* selectedObject = nullptr;
    bool revealSelection = false;   // la Hierarchy despliega los padres de la seleccion (picking)

    // Ventanas visibles
    bool showConsole = true;
    bool showInspector = true;
    bool showHierarchy = true;
    bool showConfig = true;
    bool showResources = true;
    bool showAbout = false;

    // Viewport: lo calcula DrawViewport, asi que el resto del frame usa el valor del frame anterior
    bool mouseInsideViewport = false;
    float relMouseX = 0.0f;
    float relMouseY = 0.0f;
    float viewportWidthF = 1280.0f;
    float viewportHeightF = 720.0f;
    RenderStats renderStats;   // del ultimo Render (lo muestran el Viewport y Configuration)

    // Input de la camara
    int prevMouseX = 0, prevMouseY = 0;
    bool freeLookActive = false;   // el click derecho empezó dentro del viewport
    bool orbitActive = false;      // Alt + click izquierdo empezó dentro del viewport
    bool altHeld = false;          // el picking lo consulta: Alt + click izquierdo nunca selecciona
    float wheelDelta = 0.0f;       // muescas de rueda acumuladas en el frame (dentro del viewport)
    bool firstMouse = true;

    Uint32 lastNormalsToggleMs = 0;
    static constexpr Uint32 NORMALS_TOGGLE_DEBOUNCE = 150;

    bool dockLayoutBuilt = false;

    // Hierarchy: renombrar (por UID: el popup sigue abierto varios frames y el undo podria sustituir el objeto)
    uint64_t renameTargetUid = 0;
    bool openRenamePopup = false;
    char renameBuf[128] = { 0 };

    // Configuration
    int winW = 1280, winH = 720;
    int texFilter = 0;   // 0 = linear + mipmaps, 1 = nearest
    int texWrap = 0;     // 0 = repeat, 1 = clamp, 2 = mirrored repeat
    std::vector<float> plotBuf;

    // Resources
    bool onlyLoadedResources = false;
};
