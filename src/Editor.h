#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Camera.h"

class FrameTiming;
class Renderer;
class Scene;
class UndoHistory;

// Editor del motor: la interfaz de ImGui (menu, paneles, viewport), la camara del editor, la
// seleccion y el input. Usa el motor (Scene, Renderer, FrameTiming) pero el motor no sabe nada de el.
//
// Los metodos estan repartidos en varios .cpp segun lo que dibujan:
//   Editor.cpp             Init/Shutdown, eventos, orden del frame, menu y dockspace
//   EditorCamera.cpp       input de la camara libre/orbital, captura del raton, encuadre (F)
//   PanelInspector.cpp     Inspector
//   PanelConfiguration.cpp Configuration
//   PanelHierarchy.cpp     Hierarchy y popup de renombrar
//   PanelViewport.cpp      Viewport y picking
class Editor
{
public:
    Editor(SDL_Window* window, Scene& scene, UndoHistory& history, Renderer& renderer, FrameTiming& timing);

    // Crea el contexto de ImGui y toma el estado actual de la escena como inicio del undo.
    void Init(SDL_GLContext context);
    void Shutdown();

    void ProcessEvent(const SDL_Event& e);

    // Construye y dibuja un frame entero del editor (incluido el render de la escena en el viewport).
    void DrawFrame();

    // Escape o File > Exit.
    bool WantsToQuit() const { return quitRequested; }

private:
    // --- Editor.cpp ---
    void DrawMainMenuBar();
    void DrawDockspace();
    void AssignDroppedTexture(GLuint newTex, const std::string& filePath);

    // --- EditorCamera.cpp ---
    void UpdateCamera();
    void BeginMouseCapture();
    void EndMouseCapture();
    void ReadMouseDelta(float& dx, float& dy);
    void FrameSelectedObject();

    // --- Paneles ---
    void DrawInspector();
    void DrawConfiguration();
    void DrawHierarchy();
    void DrawViewport();

    // Motor
    SDL_Window* window;
    Scene& scene;
    UndoHistory& history;
    Renderer& renderer;
    FrameTiming& timing;

    bool quitRequested = false;

    // Camara del editor y sus matrices del frame actual
    Camera camera;
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    int selectedObject = -1;

    // Ventanas visibles
    bool showConsole = true;
    bool showInspector = true;
    bool showHierarchy = true;
    bool showConfig = true;
    bool showAbout = false;

    // Viewport: lo calcula DrawViewport, asi que el resto del frame usa el valor del frame anterior
    bool mouseInsideViewport = false;
    float relMouseX = 0.0f;
    float relMouseY = 0.0f;
    float viewportWidthF = 1280.0f;
    float viewportHeightF = 720.0f;
    int drawnCount = 0;

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

    // Hierarchy: renombrar
    int renameTargetIdx = -1;
    char renameBuf[128] = { 0 };

    // Configuration
    int winW = 1280, winH = 720;
    int texFilter = 0;   // 0 = linear + mipmaps, 1 = nearest
    int texWrap = 0;     // 0 = repeat, 1 = clamp, 2 = mirrored repeat
    std::vector<float> plotBuf;
};
