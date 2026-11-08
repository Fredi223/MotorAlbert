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
class Simulation;
class UndoHistory;
struct GameObject;

// Editor del motor: la interfaz de ImGui (menu, paneles, viewport), la camara del editor, la
// seleccion y el input. Usa el motor (Scene, Renderer, FrameTiming) pero el motor no sabe nada de el.
//
// Los metodos estan repartidos en varios .cpp segun lo que dibujan:
//   Editor.cpp             Init/Shutdown, eventos, orden del frame, menu y dockspace
//   EditorSceneFile.cpp    nueva escena, guardar y abrir (.scene)
//   EditorCamera.cpp       input de la camara libre/orbital, captura del raton, encuadre (F)
//   PanelInspector.cpp     Inspector
//   PanelConfiguration.cpp Configuration
//   PanelResources.cpp     Resources (recursos importados y sus referencias)
//   PanelAssets.cpp        Assets (arbol de la carpeta Assets/, importar y borrar)
//   PanelHierarchy.cpp     Hierarchy y popup de renombrar
//   PanelViewport.cpp      Viewport y picking
class Editor
{
public:
    Editor(SDL_Window* window, Scene& scene, ResourceManager& resources, Simulation& simulation, UndoHistory& history, Renderer& renderer, FrameTiming& timing);

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

    // Nodo del arbol de Assets/ (sin los .meta) que dibuja el panel Assets. Se reconstruye solo cuando
    // cambia (importar, borrar, Refresh), no en cada frame.
    struct AssetNode {
        std::string name;
        std::string assetPath;   // relativa a Assets/, con '/'
        bool isDirectory = false;
        std::vector<AssetNode> children;
    };

    // FOV, near y far de una camara (la del editor en Configuration y las de juego en el Inspector).
    static void DrawLensFields(ComponentCamera& lens);

private:
    // --- Editor.cpp ---
    void DrawMainMenuBar();
    // Botones Play / Pause / Step / Stop, escala de tiempo y tiempo de juego (en la barra de menu).
    // Atajos: Ctrl+P = Play/Stop, Ctrl+Shift+P = Pause/continuar.
    void DrawPlayControls();
    void StopSimulation();   // restaura la escena y recupera la seleccion por UID
    void DrawDockspace();
    void ImportDroppedFile(const std::string& filePath);
    void AssignDroppedTexture(uint64_t textureUid);

    // --- EditorSceneFile.cpp: File > New / Open / Save Scene ---
    // Las escenas son .scene (JSON, SceneSerializer) en Assets/Scenes/. Rutas relativas a Assets/.
    // Abrir o crear una escena suelta la seleccion y vacia el historial de undo (un snapshot no puede
    // deshacer un cambio de escena entero).
    void NewScene();
    bool SaveScene(const std::string& assetPath);
    bool LoadScene(const std::string& assetPath);
    void DrawSceneFilePopups();
    void SetCurrentScene(const std::string& assetPath);   // tambien pone el titulo de la ventana

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
    void DrawAssets();

    // --- PanelAssets.cpp ---
    void RebuildAssetTree();
    void DrawAssetNode(const AssetNode& node);
    // Borra el asset y quita sus recursos de los GameObjects que los usan. Vacia el historial de undo:
    // borrar un archivo no se puede deshacer, y un snapshot podria devolver un recurso que ya no existe.
    void DeleteAsset(const std::string& assetPath);
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
    Simulation& simulation;
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
    bool showAssets = true;
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

    // Assets
    AssetNode assetTree;
    bool assetTreeDirty = true;
    std::string assetToDelete;   // pendiente de confirmar en el popup
    bool openDeleteAssetPopup = false;

    // Escena abierta (relativa a Assets/, p. ej. "Scenes/Street.scene"); vacia = sin guardar todavia
    std::string currentScene;
    bool openSaveScenePopup = false;
    bool openLoadScenePopup = false;
    char sceneNameBuf[128] = { 0 };
};
