#include <algorithm>
#include <cfloat>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/version.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"   // DockBuilder

#include "Camera.h"
#include "DdsLoader.h"
#include "FrameTiming.h"
#include "GameObject.h"
#include "Geometry.h"
#include "Logger.h"
#include "ModuleModelLoader.h"
#include "Octree.h"
#include "Primitives.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shaders.h"
#include "SystemInfo.h"
#include "TextureLoader.h"
#include "UndoHistory.h"

// ============================================================
// CONTROL DE CÁMARA
// ============================================================
Camera camera;
bool firstMouse = true;
int selectedObject = -1;
Uint32 lastNormalsToggleMs = 0;
const Uint32 NORMALS_TOGGLE_DEBOUNCE = 150;

// Target orbital = centro del AABB de mundo del objeto seleccionado; sin selección, el origen.
static void GetOrbitTarget(std::vector<GameObject>& objs, int sel, glm::vec3& outCenter, float& outRadius)
{
    if (sel >= 0 && sel < (int)objs.size())
    {
        GameObject& go = objs[sel];
        go.UpdateWorldAABB(); // defensivo: garantiza un AABB fresco aunque otra ruta haya tocado el transform
        outCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
        outRadius = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
    }
    else
    {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.0f;
    }
}

// ============================================================
// INICIALIZACIÓN DE SDL + OpenGL
// ============================================================
bool InitOpenGL(SDL_Window** window, SDL_GLContext* context, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        Console::LogError("Error inicializando SDL3: %s", SDL_GetError());
        return false;
    }
    Console::LogInfo("SDL3 inicializado correctamente.");


    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    *window = SDL_CreateWindow(
        "Motor Albert",
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!*window)
    {
        Console::LogError("Error creando ventana SDL3: %s", SDL_GetError());
        return false;
    }
    Console::LogInfo("Ventana creada correctamente (%dx%d).", width, height);

    *context = SDL_GL_CreateContext(*window);
    if (!*context)
    {
        Console::LogError("Error creando contexto OpenGL: %s", SDL_GetError());
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        Console::LogError("Error cargando funciones GLAD: %s", SDL_GetError());
        return false;
    }

    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);

    Console::LogInfo("OpenGL %s inicializado correctamente (GLAD).", (const char*)glGetString(GL_VERSION));
    Console::LogInfo("GPU: %s", (const char*)glGetString(GL_RENDERER));

    return true;
}

// ============================================================
// PROGRAMA PRINCIPAL
// ============================================================
int main(int argc, char* argv[])
{
    Console::Init();

    SDL_Window* window = nullptr;
    SDL_GLContext context;

    if (!InitOpenGL(&window, &context, 1280, 720))
    {
        Console::LogError("No se pudo inicializar OpenGL/SDL: %s", SDL_GetError());
        return -1;
    }

    Console::LogInfo("Inicializando motor - version E1");
    Console::LogInfo("Librerias: SDL %d.%d.%d, Assimp %u.%u.%u, ImGui %s, stb_image (texturas), GLM, GLAD",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
        aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionRevision(), IMGUI_VERSION);

    FrameTiming timing;
    timing.Init();
    timing.ApplyVsync();

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 projection = camera.GetProjectionMatrix(1280.0f / 720.0f);

    // ============================================================
    // INICIALIZAR IMGUI
    // ============================================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 460");
    Console::LogInfo("ImGui inicializado (docking, backends SDL3 + OpenGL3)");

    Renderer renderer;
    renderer.Init(1280, 720);

    // ========================================================
    // ESCENA
    // ========================================================
    // La escena empieza vacia; RebuildOctree recalcula los limites del octree cada vez que se
    // anade algo (la Baker House inicial, drag & drop o una primitiva).
    Scene scene;
    scene.defaultTexture = renderer.GetCheckerTexture();
    std::vector<GameObject>& gameObjects = scene.objects;   // alias provisional para los paneles del editor

    // El historial de undo guarda snapshots que pueden usar texturas que ya no estan en la
    // escena: la escena le pregunta antes de liberar una.
    UndoHistory history;
    scene.isTextureUsedElsewhere = [&history](GLuint tex) { return history.UsesTexture(tex); };

    bool running = true;
    SDL_Event e;

    bool showConsole = true;
    bool showInspector = true;
    bool showHierarchy = true;
    bool showConfig = true;
    bool showAbout = false;

    bool mouseInsideViewport = false;
    float relMouseX = 0.0f;
    float relMouseY = 0.0f;
    float viewportWidthF = 1280.0f;
    float viewportHeightF = 720.0f;

    int prevMouseX = 0, prevMouseY = 0;
    bool freeLookActive = false;   // el click derecho empezó dentro del viewport
    bool orbitActive = false;      // Alt + click izquierdo empezó dentro del viewport
    float wheelDelta = 0.0f;       // muescas de rueda acumuladas en el frame (dentro del viewport)

    // Captura del ratón compartida por cámara libre y orbital (son excluyentes).
    auto BeginMouseCapture = [&]() {
        float mx = 0.f, my = 0.f;
        SDL_GetMouseState(&mx, &my);
        prevMouseX = (int)mx;
        prevMouseY = (int)my;

        SDL_SetWindowRelativeMouseMode(window, true);
        SDL_HideCursor();
        firstMouse = true;
        };

    auto EndMouseCapture = [&]() {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_ShowCursor();
        SDL_WarpMouseInWindow(window, (float)prevMouseX, (float)prevMouseY);
        };

    // Delta de ratón desde el último frame (mismo en ambos modos).
    auto ReadMouseDelta = [&](float& dx, float& dy) {
        SDL_GetRelativeMouseState(&dx, &dy);
        // SDL acumula el movimiento desde la última llamada, y aquí solo se
        // llama con un botón mantenido: el primer delta trae todo el recorrido
        // del cursor hasta el viewport. Se descarta.
        if (firstMouse)
        {
            dx = dy = 0.f;
            firstMouse = false;
        }
        };

    // F: encuadra el objeto seleccionado manteniendo la dirección de mirada actual.
    auto FrameSelectedObject = [&]() {
        if (selectedObject < 0 || selectedObject >= (int)gameObjects.size())
        {
            Console::LogInfo("F: no hay ningun objeto seleccionado que encuadrar");
            return;
        }
        GetOrbitTarget(gameObjects, selectedObject, camera.orbitTarget, camera.orbitRadius);
        camera.FrameTarget(viewportHeightF > 0.0f ? viewportWidthF / viewportHeightF : 1.0f);

        Console::LogInfo("Encuadrado %s: target=(%.2f, %.2f, %.2f) distancia=%.2f",
            gameObjects[selectedObject].name.c_str(),
            camera.orbitTarget.x, camera.orbitTarget.y, camera.orbitTarget.z, camera.orbitDistance);
        };

    // Aplica una textura recien cargada (PNG/JPG/TGA/DDS soltada en la ventana) al objeto
    // seleccionado, o al primero si no hay seleccion. Si no hay objetos, la libera.
    auto AssignDroppedTexture = [&](GLuint newTex, const std::string& filePath)
        {
            int target = -1;
            if (selectedObject >= 0 && selectedObject < (int)gameObjects.size()) target = selectedObject;
            else if (!gameObjects.empty()) target = 0;

            if (target < 0)
            {
                glDeleteTextures(1, &newTex);   // nadie la va a usar
                Console::LogInfo("No hay GameObjects para aplicar la textura");
                return;
            }
            GameObject& go = gameObjects[target];
            const GLuint oldTex = go.texComp.textureID;
            go.texComp.textureID = newTex;
            go.texComp.path = filePath;
            scene.ReleaseTextureIfUnused(oldTex);
            Console::LogInfo("Textura aplicada a: %s (path: %s)", go.name.c_str(), filePath.c_str());
        };

    // Baker House se carga al arrancar desde Resources/, que vive junto al ejecutable (CMake
    // la copia ahi al compilar): asi funciona igual desde build/ que desde la carpeta de entrega.
    {
        const char* basePath = SDL_GetBasePath();
        const std::string bakerHouse = std::string(basePath ? basePath : "") + "Resources/BakerHouse.fbx";
        scene.LoadFbx(bakerHouse);
    }

    Console::LogInfo("Iniciando bucle principal");

    history.Reset(scene, selectedObject);   // estado inicial: el primer cambio apilara este en undo

    // ========================================================
    // BUCLE PRINCIPAL
    // ========================================================
    while (running)
    {
        // ========================================================
        // MANEJO DE EVENTOS SDL + IMGUI
        // ========================================================
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);

            switch (e.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            {
                int sc = e.key.scancode;
                if (sc == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                else if (sc == SDL_SCANCODE_H && !io.WantTextInput)
                {
                    if (selectedObject >= 0 && selectedObject < static_cast<int>(gameObjects.size()))
                    {
                        gameObjects[selectedObject].hidden = !gameObjects[selectedObject].hidden;

                        if (gameObjects[selectedObject].hidden)
                        {
                            Console::LogInfo("%s ocultado", gameObjects[selectedObject].name.c_str());
                            selectedObject = -1;
                        }
                        else
                        {
                            Console::LogInfo("%s mostrado", gameObjects[selectedObject].name.c_str());
                        }
                    }
                    else
                    {
                        Console::LogError("[H] No hay ningun objeto seleccionado que ocultar");
                    }
                }
                else if (sc == SDL_SCANCODE_N && !io.WantTextInput)
                {
                    Uint32 now = SDL_GetTicks();
                    if (now - lastNormalsToggleMs > NORMALS_TOGGLE_DEBOUNCE) {
                        renderer.showNormals = !renderer.showNormals;
                        Console::LogInfo("Mostrar normales: %s", renderer.showNormals ? "ON" : "OFF");
                        lastNormalsToggleMs = now;
                    }
                }
                else if (sc == SDL_SCANCODE_F && !io.WantTextInput)
                {
                    FrameSelectedObject();
                }
            }
            break;

            case SDL_EVENT_MOUSE_WHEEL:
                // Solo con el cursor sobre el viewport (dato del frame anterior, como el resto).
                if (mouseInsideViewport)
                    wheelDelta += e.wheel.y;
                break;

            case SDL_EVENT_DROP_FILE:
            {
                const char* dropped_filedir = e.drop.data;
                std::string filePath = dropped_filedir ? dropped_filedir : "";

                if (!filePath.empty())
                {
                    Console::LogInfo("Archivo arrastrado: %s", filePath.c_str());

                    std::string lower = filePath;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return std::tolower(c); });

                    if (lower.ends_with(".png") || lower.ends_with(".jpg") ||
                        lower.ends_with(".jpeg") || lower.ends_with(".tga"))
                    {
                        if (GLuint newTex = LoadImageTexture(filePath)) AssignDroppedTexture(newTex, filePath);
                    }
                    else if (lower.ends_with(".dds"))
                    {
                        if (GLuint newTex = LoadDDSTexture(filePath.c_str())) AssignDroppedTexture(newTex, filePath);
                    }
                    else if (lower.ends_with(".fbx"))
                    {
                        scene.LoadFbx(filePath);
                    }
                    else
                    {
                        Console::LogError("Archivo no soportado: %s", filePath.c_str());
                    }
                }
            }

            }
        }
        // ========================================================
        //  INICIAR FRAME IMGUI (OBLIGATORIO)
        // ========================================================
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ============================================================
        // Barra de menú principal
        // ============================================================
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history.CanUndo())) history.RequestUndo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history.CanRedo())) history.RequestRedo();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Console", nullptr, &showConsole);
                ImGui::MenuItem("Inspector", nullptr, &showInspector);
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
                ImGui::MenuItem("Configuration", nullptr, &showConfig);
                ImGui::MenuItem("Normals", nullptr, &renderer.showNormals);
                ImGui::MenuItem("Face Normals", nullptr, &renderer.showFaceNormals);
                ImGui::MenuItem("About", nullptr, &showAbout);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("GameObject"))
            {
                if (ImGui::BeginMenu("3D Object"))
                {
                    if (ImGui::MenuItem("Cube")) selectedObject = scene.AddPrimitive("Cube", CreateCubeMesh());
                    if (ImGui::MenuItem("Sphere")) selectedObject = scene.AddPrimitive("Sphere", CreateSphereMesh());
                    if (ImGui::MenuItem("Cylinder")) selectedObject = scene.AddPrimitive("Cylinder", CreateCylinderMesh());
                    if (ImGui::MenuItem("Cone")) selectedObject = scene.AddPrimitive("Cone", CreateConeMesh());
                    if (ImGui::MenuItem("Plane")) selectedObject = scene.AddPrimitive("Plane", CreatePlaneMesh());
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Documentation")) SDL_OpenURL("https://github.com/Fredi223/MotorAlbert#readme");
                if (ImGui::MenuItem("Report Bug")) SDL_OpenURL("https://github.com/Fredi223/MotorAlbert/issues");
                if (ImGui::MenuItem("Download Latest")) SDL_OpenURL("https://github.com/Fredi223/MotorAlbert/releases");
                ImGui::MenuItem("About", nullptr, &showAbout);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
        if (showAbout)
        {
            ImGui::Begin("About", &showAbout);
            ImGui::Text("Motor Albert v1.0");
            ImGui::Separator();
            ImGui::Text("Autores: Albert Frederic Bailen Ruesca");
            ImGui::Text("Librerias usadas: SDL3, OpenGL, ImGui, Assimp, stb_image, GLM");
            ImGui::Text("Licencia: MIT License");
            ImGui::End();
        }

        // ========================================================
        //  CONTROL DE CÁMARA: LIBRE (click derecho) Y ORBITAL (Alt + click izquierdo)
        // ========================================================
        bool rightClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool rightClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        bool rightClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        bool leftClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool leftClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool leftClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        bool altHeld = io.KeyAlt; // el picking (más abajo) lo consulta: Alt + click izquierdo nunca selecciona

        // Entrada en cada modo. Ambos capturan el ratón, así que son excluyentes.
        // mouseInsideViewport lo calcula el panel "Viewport" más abajo en el
        // frame, así que aquí vale el del frame anterior (mismo dato que usa el picking).
        if (mouseInsideViewport && !freeLookActive && !orbitActive)
        {
            if (rightClickPressed)
            {
                BeginMouseCapture();
                freeLookActive = true;
            }
            else if (leftClickPressed && altHeld)
            {
                BeginMouseCapture();
                orbitActive = true;

                // Target: objeto seleccionado (o el origen). distance: la actual cámara-target.
                GetOrbitTarget(gameObjects, selectedObject, camera.orbitTarget, camera.orbitRadius);
                camera.BeginOrbit();
            }
        }

        if (rightClickReleased && freeLookActive)
        {
            EndMouseCapture();
            freeLookActive = false;
        }

        if (leftClickReleased && orbitActive)
        {
            EndMouseCapture();
            orbitActive = false;
        }

        if (freeLookActive && rightClickHeld)
        {
            io.WantCaptureMouse = false;
            io.WantCaptureKeyboard = false;

            float dx, dy;
            ReadMouseDelta(dx, dy);
            camera.Rotate(dx, dy);

            // Duración del último frame completo (lo mide FrameTiming).
            float deltaTime = timing.LastFrameSeconds();

            const bool* keys = SDL_GetKeyboardState(NULL);
            float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
            float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
            float upAxis = (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
            bool sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            camera.Move(forward, right, upAxis, deltaTime, sprint);
        }

        if (orbitActive && leftClickHeld)
        {
            float dx, dy;
            ReadMouseDelta(dx, dy);
            camera.Orbit(dx, dy);
        }

        // Rueda: fuera del modo orbital, el target de referencia es el objeto seleccionado.
        if (wheelDelta != 0.0f)
        {
            if (!orbitActive)
                GetOrbitTarget(gameObjects, selectedObject, camera.orbitTarget, camera.orbitRadius);
            camera.Zoom(wheelDelta, orbitActive);
        }
        wheelDelta = 0.0f;

        // Único punto donde se recalcula view: refleja la cámara tras cualquier modo, la rueda
        // o F. El picking usa camera.position como origen del rayo, y debe coincidir.
        view = camera.GetViewMatrix();

        // ============================================================
        //  Actualizar proyección según el tamaño del viewport
        // ============================================================
        projection = camera.GetProjectionMatrix(viewportWidthF / viewportHeightF);

        // ----------------------- DOCKSPACE PRINCIPAL -----------------------
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        static bool first_time = true;
        if (first_time)
        {
            first_time = false;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
            ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);      // centro
            ImGui::DockBuilderDockWindow("Hierarchy", dock_left);        // izquierda
            ImGui::DockBuilderDockWindow("Inspector", dock_right);       // derecha

            ImGui::DockBuilderDockWindow("Configuration", dock_bottom);
            ImGui::DockBuilderDockWindow("Console", dock_bottom);        // abajo
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::End();

        // -------------------- INSPECTOR --------------------
        if (showInspector)
        {
            ImGui::Begin("Inspector");

            if (selectedObject >= 0 && selectedObject < (int)gameObjects.size())
            {
                GameObject& go = gameObjects[selectedObject];
                char nameBuf[128];
                std::strncpy(nameBuf, go.name.c_str(), sizeof(nameBuf));
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                {
                    go.name = std::string(nameBuf);
                }

                ImGui::Separator();

                // ---------------- Transform ----------------
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // Cualquier cambio del transform deja desactualizado el AABB de mundo cacheado
                    // (worldAabbMin/Max), que usan el culling y el picking, y la posicion del
                    // objeto dentro del octree.
                    bool transformChanged = false;    // hubo un cambio de valor en este frame
                    bool transformCommitted = false;  // el gesto de edicion (arrastre o tecleo) ha terminado

                    if (ImGui::DragFloat3("Position", glm::value_ptr(go.transform.position), 0.1f))
                    {
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(go.transform.rotation), 0.5f))
                    {
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::DragFloat3("Scale", glm::value_ptr(go.transform.scale), 0.05f))
                    {
                        go.transform.scale = glm::max(go.transform.scale, glm::vec3(0.001f));
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Button("Reset Transform")) {
                        go.transform.position = glm::vec3(0.0f);
                        go.transform.rotation = glm::vec3(0.0f);
                        go.transform.scale = glm::vec3(1.0f);
                        transformChanged = true;
                        transformCommitted = true;
                    }

                    // El AABB del objeto se refresca en cada cambio (8 esquinas, barato), asi el
                    // culling y el picking de este mismo frame ya usan la posicion real...
                    if (transformChanged)
                    {
                        go.UpdateWorldAABB();
                    }
                    // ...pero el octree solo sabe reconstruirse entero (y cada reconstruccion escribe
                    // en la consola): se reconstruye una vez al terminar el gesto, no en cada frame
                    // de un arrastre. Mientras se arrastra, la consulta de dibujo (caja de +-1000)
                    // sigue devolviendo el objeto aunque su hoja del octree sea la antigua.
                    if (transformCommitted)
                    {
                        scene.structureDirty = true;
                    }
                }

                ImGui::Separator();

                // ---------------- Mesh ----------------
                if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const Mesh& mesh = go.meshComp.mesh;
                    ImGui::Text("Vertices: %u", mesh.vertexCount);
                    ImGui::Text("Indices: %u", mesh.indexCount);
                    ImGui::Text("VAO: %u", mesh.VAO);
                    ImGui::Text("TexturePath: %s", mesh.texturePath.empty() ? "<none>" : mesh.texturePath.c_str());

                    static const char* normalsModes[] = { "Use Global", "Force On", "Force Off" };
                    int mode = static_cast<int>(go.normalsMode);
                    if (ImGui::Combo("Normals Mode", &mode, normalsModes, IM_ARRAYSIZE(normalsModes)))
                    {
                        go.normalsMode = static_cast<GameObject::NormalsMode>(mode);
                    }
                    bool forceOnNormals = (go.normalsMode == GameObject::NM_ForceOn);
                    if (ImGui::Checkbox("Mostrar normales (mesh)", &forceOnNormals))
                    {
                        go.normalsMode = forceOnNormals ? GameObject::NM_ForceOn : GameObject::NM_UseGlobal;
                        Console::LogInfo("Mostrar normales (local) de %s: %s", go.name.c_str(), normalsModes[go.normalsMode]);
                    }

                    ImGui::Checkbox("Mostrar normales por cara (mesh)", &go.showFaceNormals);
                    ImGui::TextDisabled("Global Normals: %s | Face Normals: %s", renderer.showNormals ? "ON" : "OFF", renderer.showFaceNormals ? "ON" : "OFF");
                }

                ImGui::Separator();

                // ---------------- Texture ----------------
                if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("TextureID: %u", go.texComp.textureID);
                    ImGui::TextWrapped("Path: %s", go.texComp.path.empty() ? "<none>" : go.texComp.path.c_str());

                    if (go.texComp.textureID != 0)
                    {
                        // El tamano se pregunta a OpenGL (nivel 0 de la textura) en vez de guardarlo,
                        // asi vale para cualquier origen: FBX, drag & drop o checker.
                        GLint texW = 0, texH = 0;
                        glGetTextureLevelParameteriv(go.texComp.textureID, 0, GL_TEXTURE_WIDTH, &texW);
                        glGetTextureLevelParameteriv(go.texComp.textureID, 0, GL_TEXTURE_HEIGHT, &texH);
                        ImGui::Text("Size: %d x %d", texW, texH);

                        ImGui::Image((ImTextureID)(intptr_t)go.texComp.textureID, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
                        if (ImGui::Button("Remove Texture"))
                        {
                            const GLuint oldTex = go.texComp.textureID;
                            go.texComp.textureID = 0;
                            go.texComp.path.clear();
                            scene.ReleaseTextureIfUnused(oldTex);
                            Console::LogInfo("Texture removed from %s", go.name.c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Open Folder")) {
                            if (!go.texComp.path.empty()) {
                                // Sin shell de por medio: una ruta con comillas o '&' no puede
                                // inyectar comandos, como pasaba al montar la linea de explorer.
                                const std::string dir = std::filesystem::path(go.texComp.path).parent_path().string();
                                SDL_OpenURL(dir.c_str());
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("No texture assigned");
                    }

                    if (ImGui::Button("Assign CheckerTexture"))
                    {
                        const GLuint oldTex = go.texComp.textureID;
                        go.texComp.textureID = renderer.GetCheckerTexture();
                        go.texComp.path.clear();
                        scene.ReleaseTextureIfUnused(oldTex);
                        Console::LogInfo("Checker texture asignada a %s", go.name.c_str());
                    }
                }

            }
            else
            {
                ImGui::Text("No hay ningun objeto seleccionado");
            }

            ImGui::End();
        }
        // ========================= Configuration Window =========================
        if (showConfig) {
            ImGui::Begin("Configuration", &showConfig);

            if (ImGui::CollapsingHeader("System / Graphics Info", ImGuiTreeNodeFlags_DefaultOpen)) {

                ImGui::Text("SDL Version: %d.%d.%d",
                    SDL_MAJOR_VERSION,
                    SDL_MINOR_VERSION,
                    SDL_MICRO_VERSION);

                ImGui::Text("CPU Cores: %d", SDL_GetNumLogicalCPUCores());
                ImGui::Text("CPU Cacheline: %d bytes", SDL_GetCPUCacheLineSize());
                ImGui::Text("System RAM: %d MB", SDL_GetSystemRAM());

                // OpenGL / GPU
                const GLubyte* glVendor = glGetString(GL_VENDOR);
                const GLubyte* glRenderer = glGetString(GL_RENDERER);
                const GLubyte* glVersion = glGetString(GL_VERSION);
                const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
                ImGui::Separator();
                ImGui::Text("GPU Vendor: %s", glVendor ? (const char*)glVendor : "unknown");
                ImGui::Text("GPU Renderer: %s", glRenderer ? (const char*)glRenderer : "unknown");
                ImGui::Text("OpenGL Version: %s", glVersion ? (const char*)glVersion : "unknown");
                ImGui::Text("GLSL Version: %s", glslVersion ? (const char*)glslVersion : "unknown");

                ImGui::Text("Assimp: %u.%u.%u", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionRevision());
            }

            // --- Viewport / Window config ---
            if (ImGui::CollapsingHeader("Window / Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
                static int winW = 1280, winH = 720;
                ImGui::InputInt("Width", &winW);
                ImGui::InputInt("Height", &winH);
                if (ImGui::Button("Apply Window Size")) {
                    SDL_SetWindowSize(window, winW, winH);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Window Size")) {
                    SDL_SetWindowSize(window, 1280, 720);
                }

                ImGui::Checkbox("VSync", &timing.vsync);
                if (ImGui::IsItemEdited()) {
                    timing.ApplyVsync();
                }

                ImGui::InputInt("FPS Cap (0 = uncapped)", &timing.fpsCap);
                if (timing.fpsCap < 0) timing.fpsCap = 0;

                ImGui::ColorEdit3("Viewport Clear Color", glm::value_ptr(renderer.clearColor));

                ImGui::Separator();
                ImGui::Text("Culling");

                ImGui::Checkbox("Enable Frustum Culling", &renderer.frustumCulling);
            }

            // --- Input / camara ---
            if (ImGui::CollapsingHeader("Input / Camera")) {
                ImGui::DragFloat("Move speed (units/s)", &camera.moveSpeed, 0.1f, 0.1f, 100.0f);
                ImGui::DragFloat("Mouse sensitivity", &camera.mouseSensitivity, 0.005f, 0.01f, 1.0f);
                ImGui::DragFloat("Zoom step (wheel)", &camera.zoomStep, 0.005f, 0.01f, 0.5f);
                ImGui::Text("Sprint (Shift): x%.1f", Camera::SPRINT_MULTIPLIER);
                ImGui::Text("Camera: (%.2f, %.2f, %.2f)  yaw %.1f  pitch %.1f",
                    camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch);
            }

            // --- Texturas ---
            if (ImGui::CollapsingHeader("Textures")) {
                static int texFilter = 0;   // 0 = linear + mipmaps, 1 = nearest
                static int texWrap = 0;     // 0 = repeat, 1 = clamp, 2 = mirrored repeat
                const char* filters[] = { "Linear + mipmaps", "Nearest" };
                const char* wraps[] = { "Repeat", "Clamp to edge", "Mirrored repeat" };
                const bool filterChanged = ImGui::Combo("Filter", &texFilter, filters, IM_ARRAYSIZE(filters));
                const bool wrapChanged = ImGui::Combo("Wrap", &texWrap, wraps, IM_ARRAYSIZE(wraps));
                ImGui::TextDisabled("Se aplica a las texturas ya cargadas");

                std::vector<GLuint> texIds{ renderer.GetCheckerTexture() };
                for (const GameObject& o : gameObjects)
                    if (o.texComp.textureID != 0) texIds.push_back(o.texComp.textureID);
                std::sort(texIds.begin(), texIds.end());
                texIds.erase(std::unique(texIds.begin(), texIds.end()), texIds.end());

                if (filterChanged || wrapChanged) {
                    const GLint minF = texFilter == 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST;
                    const GLint magF = texFilter == 0 ? GL_LINEAR : GL_NEAREST;
                    const GLint wrap = texWrap == 0 ? GL_REPEAT : (texWrap == 1 ? GL_CLAMP_TO_EDGE : GL_MIRRORED_REPEAT);
                    for (GLuint t : texIds) {
                        glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, minF);
                        glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, magF);
                        glTextureParameteri(t, GL_TEXTURE_WRAP_S, wrap);
                        glTextureParameteri(t, GL_TEXTURE_WRAP_T, wrap);
                    }
                    Console::LogInfo("Texturas: filtro=%s, wrap=%s (%zu texturas)", filters[texFilter], wraps[texWrap], texIds.size());
                }

                // Estimacion de VRAM: RGBA8 (4 B/pixel) + ~1/3 extra de la cadena de mipmaps.
                double texBytes = 0.0;
                for (GLuint t : texIds) {
                    GLint w = 0, h = 0;
                    glGetTextureLevelParameteriv(t, 0, GL_TEXTURE_WIDTH, &w);
                    glGetTextureLevelParameteriv(t, 0, GL_TEXTURE_HEIGHT, &h);
                    texBytes += double(w) * h * 4.0 * (4.0 / 3.0);
                }
                ImGui::Text("Loaded textures: %zu (~%.2f MB)", texIds.size(), texBytes / (1024.0 * 1024.0));
            }

            // --- Memoria ---
            if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
                size_t workingSet = 0, privateBytes = 0;
                if (SystemInfo::GetProcessMemory(workingSet, privateBytes)) {
                    ImGui::Text("Process RAM (working set): %.1f MB", workingSet / (1024.0 * 1024.0));
                    ImGui::Text("Process committed (private): %.1f MB", privateBytes / (1024.0 * 1024.0));
                }
                else {
                    ImGui::TextDisabled("Process memory: not available on this platform");
                }

                size_t vertexCount = 0, indexCount = 0, meshBytes = 0;
                for (const GameObject& o : gameObjects) {
                    const Mesh& m = o.meshComp.mesh;
                    vertexCount += m.vertexCount;
                    indexCount += m.indexCount;
                    meshBytes += m.vertices.size() * sizeof(float) + m.indices.size() * sizeof(unsigned int);
                }
                ImGui::Text("Scene: %zu objects, %zu vertices, %zu indices", gameObjects.size(), vertexCount, indexCount);
                ImGui::Text("Mesh data: %.2f MB (CPU, same size on GPU)", meshBytes / (1024.0 * 1024.0));
            }

            // --- Performance / graphs ---
            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                float avg = 0.0f;
                for (float t : timing.FrameTimesMs()) avg += t;
                avg = timing.FrameTimesMs().empty() ? 0.0f : avg / (float)timing.FrameTimesMs().size();
                float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;

                ImGui::Text("FPS (avg): %.1f", fps);
                ImGui::Text("Frame ms (avg): %.3f", avg);
                // PlotLines necesita memoria contigua y un deque no lo es: se copia a un vector.
                static std::vector<float> plotBuf;
                plotBuf.assign(timing.FrameTimesMs().begin(), timing.FrameTimesMs().end());

                ImGui::PlotLines("Frame ms", plotBuf.data(), (int)plotBuf.size(), 0, nullptr, 0.0f, 50.0f, ImVec2(0, 80));
                ImGui::Text("Last frame time: %.3f ms", plotBuf.empty() ? 0.0f : plotBuf.back());
            }

            ImGui::End();
        }
        // Render de la escena en el framebuffer del viewport. Va aqui, antes de Hierarchy y del undo,
        // porque esos pueden borrar o sustituir objetos y el octree no se reconstruye hasta despues.
        int drawnCount = renderer.Render(scene, view, projection);

        Console::DrawConsole("Console", &showConsole);

        // -------------------- Hierarchy panel (drag/rename/dup/delete) --------------------
        ImGui::Begin("Hierarchy");

        static int rename_target_idx = -1;
        static char renameBuf[128] = { 0 };

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (selectedObject >= 0 && selectedObject < (int)gameObjects.size()) {
                    scene.Erase(selectedObject, selectedObject);
                }
            }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
                if (selectedObject >= 0 && selectedObject < (int)gameObjects.size()) {
                    int newIdx = scene.Duplicate(selectedObject);
                    if (newIdx != -1) selectedObject = newIdx;
                }
            }
        }

        for (int i = 0; i < (int)gameObjects.size(); ++i)
        {
            ImGui::PushID(i);

            ImVec4 textColor = gameObjects[i].hidden ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);

            bool isSelected = (selectedObject == i);
            if (ImGui::Selectable(gameObjects[i].name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedObject = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    rename_target_idx = i;
                    std::strncpy(renameBuf, gameObjects[i].name.c_str(), sizeof(renameBuf) - 1);
                    renameBuf[sizeof(renameBuf) - 1] = '\0';
                    ImGui::OpenPopup("Rename Popup");
                }
            }
            if (ImGui::BeginPopupContextItem(nullptr))
            {
                if (ImGui::MenuItem("Rename")) {
                    rename_target_idx = i;
                    std::strncpy(renameBuf, gameObjects[i].name.c_str(), sizeof(renameBuf) - 1);
                    renameBuf[sizeof(renameBuf) - 1] = '\0';
                    ImGui::OpenPopup("Rename Popup");
                }
                if (ImGui::MenuItem("Duplicate")) {
                    int newIdx = scene.Duplicate(i);
                    if (newIdx != -1) {
                        selectedObject = newIdx;
                        Console::LogInfo("Duplicated %s -> %s", gameObjects[i].name.c_str(), gameObjects[newIdx].name.c_str());
                    }
                }
                if (ImGui::MenuItem("Delete")) {
                    scene.Erase(i, selectedObject);
                }
                ImGui::EndPopup();
            }

            // Arrastrar un objeto sobre otro lo mueve a esa posicion de la lista.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload("DND_GAMEOBJECT", &i, sizeof(int));   // ImGui copia el payload
                ImGui::Text("%s", gameObjects[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_GAMEOBJECT"))
                {
                    int src = *(const int*)payload->Data;
                    int dst = i;
                    if (src != dst && src >= 0 && src < (int)gameObjects.size())
                    {
                        GameObject tmp = std::move(gameObjects[src]);
                        gameObjects.erase(gameObjects.begin() + src);
                        gameObjects.insert(gameObjects.begin() + dst, std::move(tmp));
                        if (selectedObject == src) selectedObject = dst;
                        else if (src < dst && selectedObject > src && selectedObject <= dst) selectedObject--;
                        else if (src > dst && selectedObject >= dst && selectedObject < src) selectedObject++;
                        scene.structureDirty = true;
                        Console::LogInfo("Reordered: %d -> %d", src, dst);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        // Ctrl+Z / Ctrl+Y (o Ctrl+Shift+Z). Va antes de reconstruir el octree porque deshacer
        // sustituye la escena entera.
        if (io.KeyCtrl && !io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) history.RequestRedo(); else history.RequestUndo(); }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y)) history.RequestRedo();
        }
        history.Update(scene, selectedObject);

        // Reconstruir el octree justo aqui (y no al final del frame): el panel Viewport, mas
        // abajo, hace picking consultando el octree en este mismo frame.
        if (scene.structureDirty)
        {
            scene.RebuildOctree();
            scene.structureDirty = false;
        }

        if (ImGui::BeginPopupModal("Rename Popup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (rename_target_idx >= 0 && rename_target_idx < (int)gameObjects.size())
            {
                ImGui::Text("Rename object:");
                ImGui::InputText("##rename", renameBuf, IM_ARRAYSIZE(renameBuf));
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    gameObjects[rename_target_idx].name = std::string(renameBuf);
                    ImGui::CloseCurrentPopup();
                    rename_target_idx = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                    rename_target_idx = -1;
                }
            }
            else
            {
                ImGui::Text("Target no valido");
                if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); rename_target_idx = -1; }
            }
            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::Begin("Viewport");
        ImGui::TextColored(
            renderer.frustumCulling ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Frustum Culling: %s",
            renderer.frustumCulling ? "ON" : "OFF"
        );

        ImGui::Text("Drawn objects: %d", drawnCount);
        ImGui::Separator();

        ImVec2 size = ImGui::GetContentRegionAvail();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 mousePos = ImGui::GetMousePos();

        relMouseX = mousePos.x - cursorPos.x;
        relMouseY = mousePos.y - cursorPos.y;
        mouseInsideViewport =
            (relMouseX >= 0 && relMouseX < size.x &&
                relMouseY >= 0 && relMouseY < size.y);

        // Minimo 1 px: con el panel colapsado o minimizado size puede ser 0, y un ancho/alto 0
        // daria un framebuffer vacio y una proyeccion con aspect = x/0 (infinito o NaN).
        const int newViewportW = std::max(1, (int)size.x);
        const int newViewportH = std::max(1, (int)size.y);
        if (newViewportW != renderer.GetViewportWidth() || newViewportH != renderer.GetViewportHeight())
        {
            viewportWidthF = (float)newViewportW;
            viewportHeightF = (float)newViewportH;
            renderer.ResizeViewport(newViewportW, newViewportH);
        }

        // ========================================================
        // SELECCIÓN DE OBJETOS (RAYCAST CONTRA LOS AABB)
        // ========================================================
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !altHeld && mouseInsideViewport && !ImGui::IsAnyItemHovered())
        {
            const float PICK_TOLERANCE_PIXELS = 3.0f;   // margen de click alrededor del AABB, en pixeles de pantalla
            const float pickPixelToWorld = 2.0f * std::tan(glm::radians(Camera::FOV_DEG) * 0.5f) / viewportHeightF; // mundo por pixel y por unidad de profundidad

            // relMouseX/Y ya son relativos al viewport (origen arriba-izq), tal como los espera la funcion
            glm::vec3 rayDir = ScreenToWorldRay_UnProject(
                relMouseX, relMouseY,
                viewportWidthF, viewportHeightF,
                projection, view);

            glm::vec3 rayOrigin = camera.position;

            float closestDistance = std::numeric_limits<float>::max();
            int hitIndex = -1;

            // Candidatos: los objetos de las hojas del octree que toca la caja que envuelve el rayo.
            const glm::vec3 rayEnd = rayOrigin + rayDir * 10000.0f;
            std::vector<GameObject*> candidates;
            scene.octree.Query(glm::min(rayOrigin, rayEnd), glm::max(rayOrigin, rayEnd), candidates);

            for (GameObject* goPtr : candidates)
            {
                GameObject& go = *goPtr;
                int i = int(&go - &gameObjects[0]);
                if (go.hidden || go.meshComp.mesh.vertices.empty()) continue;

                float tHit = 0.0f;

                // Tolerancia de click: como maximo PICK_TOLERANCE_PIXELS pixeles alrededor del AABB. Un
                // pixel a profundidad d equivale a d * 2*tan(fov/2) / altoViewport unidades de mundo, asi el
                // margen es parecido en pantalla cerca o lejos (uno fijo en mundo no lo es). Se usa una cota
                // inferior de la profundidad de cualquier punto del AABB (la del centro menos la semidiagonal)
                // para que en sus caras mas cercanas, donde un pixel mide menos, el margen no se dispare.
                const glm::vec3 aabbCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
                const float halfDiag = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
                const float depth = std::max(glm::dot(aabbCenter - rayOrigin, camera.front) - halfDiag, 0.1f);
                const float pickInflate = PICK_TOLERANCE_PIXELS * depth * pickPixelToWorld;

                glm::vec3 pickMin = go.worldAabbMin - glm::vec3(pickInflate);
                glm::vec3 pickMax = go.worldAabbMax + glm::vec3(pickInflate);

                if (!RayIntersectsAABB(rayOrigin, rayDir, pickMin, pickMax, tHit))
                    continue;

                // rayDir esta normalizado, asi que tHit ya es la distancia en unidades de mundo.
                if (tHit > 0.0f && tHit < closestDistance)
                {
                    closestDistance = tHit;
                    hitIndex = i;
                }
            }

            if (hitIndex != -1)
            {
                selectedObject = hitIndex;
                Console::LogInfo("Seleccionado: %s (index=%d)", gameObjects[selectedObject].name.c_str(), selectedObject);
            }
            else
            {
                selectedObject = -1;
                Console::LogInfo("Seleccion: ninguno");
            }
        }

        ImGui::Image((ImTextureID)(intptr_t)renderer.GetViewportTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End(); // cierra Viewport

        // ============================================================
        // RENDERIZAR IMGUI (UNA VEZ POR FRAME)
        // ============================================================
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }
        timing.EndFrame();
        SDL_GL_SwapWindow(window);
    }

    // ========================================================
    // LIMPIEZA
    // ========================================================
    Console::LogInfo("Saliendo del bucle principal");

    for (auto& go : gameObjects) go.Destroy();
    gameObjects.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    renderer.Shutdown();

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
