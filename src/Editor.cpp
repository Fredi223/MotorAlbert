#include "Editor.h"
#include <algorithm>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"   // DockBuilder

#include "FrameTiming.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scene.h"
#include "Simulation.h"
#include "ResourceManager.h"
#include "UndoHistory.h"

Editor::Editor(SDL_Window* window, Scene& scene, ResourceManager& resources, Simulation& simulation, UndoHistory& history, Renderer& renderer, FrameTiming& timing)
    : window(window), scene(scene), resources(resources), simulation(simulation), history(history), renderer(renderer), timing(timing)
{
}

void Editor::Init(SDL_GLContext context)
{
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

    history.Reset(scene, selectedObject);   // estado inicial: el primer cambio apilara este en undo
    SetCurrentScene({});
}

void Editor::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// ========================================================
// MANEJO DE EVENTOS SDL + IMGUI
// ========================================================
void Editor::ProcessEvent(const SDL_Event& e)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplSDL3_ProcessEvent(&e);

    switch (e.type)
    {
    case SDL_EVENT_QUIT:
        quitRequested = true;
        break;
    case SDL_EVENT_KEY_DOWN:
    {
        int sc = e.key.scancode;
        if (sc == SDL_SCANCODE_ESCAPE) {
            quitRequested = true;
        }
        else if (sc == SDL_SCANCODE_H && !io.WantTextInput)
        {
            if (selectedObject)
            {
                selectedObject->hidden = !selectedObject->hidden;

                if (selectedObject->hidden)
                {
                    Console::LogInfo("%s ocultado (con sus hijos)", selectedObject->name.c_str());
                    selectedObject = nullptr;
                }
                else
                {
                    Console::LogInfo("%s mostrado", selectedObject->name.c_str());
                }
            }
            else
            {
                Console::LogError("[H] No hay ningun objeto seleccionado que ocultar");
            }
        }
        else if (sc == SDL_SCANCODE_N && !io.WantTextInput)
        {
            const Uint64 now = SDL_GetTicks();
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
        if (e.drop.data) ImportDroppedFile(e.drop.data);
        break;
    }
}

// El orden importa: el picking del Viewport y el render consultan el octree, que guarda punteros
// a los GameObject. Hierarchy y el undo pueden borrar o sustituir objetos, así que el render va
// antes que ellos y el octree se reconstruye después, antes del Viewport.
void Editor::DrawFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    DrawMainMenuBar();
    UpdateCamera();
    DrawDockspace();
    DrawInspector();
    DrawConfiguration();
    DrawResources();
    DrawAssets();

    // Logica de juego (solo en Play): antes del render, para dibujar ya el estado de este frame.
    simulation.Tick(scene, timing.LastFrameSeconds());

    // Matrices de mundo al dia con lo editado este frame (Inspector) antes de dibujar.
    scene.UpdateTransforms();
    renderStats = renderer.Render(scene, view, projection);

    Console::DrawConsole("Console", &showConsole);
    DrawHierarchy();

    // Ctrl+Z / Ctrl+Y (o Ctrl+Shift+Z). Va antes de reconstruir el octree porque deshacer
    // sustituye la escena entera. En Play no hay undo: lo que cambia la simulacion no son ediciones
    // (Stop ya lo deshace todo).
    const bool editing = simulation.IsStopped();
    if (editing && io.KeyCtrl && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) history.RequestRedo(); else history.RequestUndo(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y)) history.RequestRedo();
        else if (ImGui::IsKeyPressed(ImGuiKey_S)) { if (currentScene.empty()) openSaveScenePopup = true; else SaveScene(currentScene); }
    }
    if (editing) history.Update(scene, selectedObject);

    // Reconstruir el octree justo aqui (y no al final del frame): el panel Viewport hace picking
    // consultando el octree en este mismo frame.
    if (scene.structureDirty)
    {
        scene.RebuildOctree();
        scene.structureDirty = false;
    }

    DrawViewport();

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
}

void Editor::DrawMainMenuBar()
{
    // ============================================================
    // Barra de menú principal
    // ============================================================
    // En Play la barra se tine (azul; naranja en pausa) para que se vea que nada de lo que se haga se conserva.
    const bool tinted = !simulation.IsStopped();
    if (tinted)
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, simulation.GetState() == Simulation::State::Paused
            ? ImVec4(0.45f, 0.30f, 0.05f, 1.0f) : ImVec4(0.10f, 0.25f, 0.50f, 1.0f));
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene")) NewScene();
            if (ImGui::MenuItem("Open Scene...")) openLoadScenePopup = true;
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            {
                if (currentScene.empty()) openSaveScenePopup = true; else SaveScene(currentScene);
            }
            if (ImGui::MenuItem("Save Scene As...")) openSaveScenePopup = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) quitRequested = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, simulation.IsStopped() && history.CanUndo())) history.RequestUndo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, simulation.IsStopped() && history.CanRedo())) history.RequestRedo();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Console", nullptr, &showConsole);
            ImGui::MenuItem("Inspector", nullptr, &showInspector);
            ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
            ImGui::MenuItem("Configuration", nullptr, &showConfig);
            ImGui::MenuItem("Assets", nullptr, &showAssets);
            ImGui::MenuItem("Resources", nullptr, &showResources);
            ImGui::MenuItem("Normals", nullptr, &renderer.showNormals);
            ImGui::MenuItem("Face Normals", nullptr, &renderer.showFaceNormals);
            if (ImGui::BeginMenu("Debug Draw"))
            {
                ImGui::MenuItem("AABBs", nullptr, &renderer.showAABBs);
                ImGui::MenuItem("Octree", nullptr, &renderer.showOctree);
                ImGui::MenuItem("Pick Ray", nullptr, &renderer.showPickRay);
                ImGui::MenuItem("Camera Frustums", nullptr, &renderer.showCameraFrustums);
                ImGui::EndMenu();
            }
            ImGui::MenuItem("About", nullptr, &showAbout);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("GameObject"))
        {
            if (ImGui::MenuItem("Create Empty")) selectedObject = scene.CreateEmpty("GameObject");
            if (ImGui::MenuItem("Create Empty Child", nullptr, false, selectedObject != nullptr))
            {
                selectedObject = scene.CreateEmpty("GameObject", selectedObject);
                revealSelection = true;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("3D Object"))
            {
                if (ImGui::MenuItem("Cube")) selectedObject = scene.AddPrimitive("Cube", resources.GetMesh(ResourceManager::UID_CUBE));
                if (ImGui::MenuItem("Sphere")) selectedObject = scene.AddPrimitive("Sphere", resources.GetMesh(ResourceManager::UID_SPHERE));
                if (ImGui::MenuItem("Cylinder")) selectedObject = scene.AddPrimitive("Cylinder", resources.GetMesh(ResourceManager::UID_CYLINDER));
                if (ImGui::MenuItem("Cone")) selectedObject = scene.AddPrimitive("Cone", resources.GetMesh(ResourceManager::UID_CONE));
                if (ImGui::MenuItem("Plane")) selectedObject = scene.AddPrimitive("Plane", resources.GetMesh(ResourceManager::UID_PLANE));
                ImGui::EndMenu();
            }
            // La camara nueva se coloca donde esta la del editor y mirando hacia donde mira ella
            // (la inversa de la vista: una camara mira hacia su -Z).
            if (ImGui::MenuItem("Camera"))
            {
                selectedObject = scene.CreateCamera(glm::inverse(view));
                Console::LogInfo("Camara de juego creada en la posicion de la camara del editor");
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

        DrawPlayControls();
        ImGui::EndMainMenuBar();
    }
    if (tinted) ImGui::PopStyleColor();
    DrawSceneFilePopups();
    if (showAbout)
    {
        ImGui::Begin("About", &showAbout);
        ImGui::Text("Motor Albert v2.0");
        ImGui::Separator();
        ImGui::Text("Autores: Albert Frederic Bailen Ruesca");
        ImGui::Text("Librerias usadas: SDL3, OpenGL (GLAD), ImGui, Assimp, stb_image, GLM, nlohmann-json");
        ImGui::Text("Licencia: MIT License");
        ImGui::End();
    }
}

void Editor::DrawDockspace()
{
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

    if (!dockLayoutBuilt)
    {
        dockLayoutBuilt = true;

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
        ImGui::DockBuilderDockWindow("Assets", dock_bottom);
        ImGui::DockBuilderDockWindow("Resources", dock_bottom);
        ImGui::DockBuilderDockWindow("Console", dock_bottom);        // abajo
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

// Un archivo soltado en la ventana se copia a Assets/ (a la carpeta abierta en el explorador, si viene de fuera) y se importa a Library.
// Un FBX se instancia en la escena; una textura se aplica al objeto seleccionado.
void Editor::ImportDroppedFile(const std::string& filePath)
{
    Console::LogInfo("Archivo arrastrado: %s", filePath.c_str());
    bool supported = false;
    const ResourceType type = ResourceManager::TypeOfExtension(filePath, supported);
    if (!supported)
    {
        Console::LogError("Archivo no soportado: %s", filePath.c_str());
        return;
    }

    // Con el explorador abierto, va a la carpeta que se esta viendo; si no, a la raiz de Assets/.
    const uint64_t uid = resources.ImportExternalFile(filePath, showAssets ? CurrentAssetDirectory() : std::string());
    assetTreeDirty = true;   // puede haber archivos nuevos en Assets/
    if (uid == 0) return;
    if (type == ResourceType::Texture)
        AssignDroppedTexture(uid);
    else if (GameObject* model = scene.InstantiateModel(uid, resources))
        selectedObject = model;
}

// Aplica una textura al objeto seleccionado si tiene malla, o al primero con malla si no.
void Editor::AssignDroppedTexture(uint64_t textureUid)
{
    GameObject* target = (selectedObject && selectedObject->HasMesh()) ? selectedObject : nullptr;
    if (!target)
        scene.ForEach([&target](GameObject& o) { if (!target && o.HasMesh()) target = &o; });
    if (!target)
    {
        Console::LogInfo("No hay GameObjects con malla para aplicar la textura (queda importada en Assets/)");
        return;
    }

    ComponentMaterial& material = target->components.Add<ComponentMaterial>();   // lo crea si no tenia
    material.texture = resources.GetTexture(textureUid);   // la anterior se descarga sola si era su ultima referencia
    const ResourceManager::Entry* entry = resources.Find(textureUid);
    Console::LogInfo("Textura aplicada a: %s (%s)", target->name.c_str(), entry ? entry->name.c_str() : "?");
}

// Controles de la simulacion, centrados en la barra de menu. Van antes del render: Stop sustituye la
// escena (y reconstruye el octree), asi que el render de este frame ya ve la escena restaurada.
void Editor::DrawPlayControls()
{
    const Simulation::State state = simulation.GetState();
    const ImGuiIO& io = ImGui::GetIO();

    // Atajos: Ctrl+P = Play/Stop, Ctrl+Shift+P = Pause/continuar.
    bool togglePlay = false, togglePause = false;
    if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_P, false))
        (io.KeyShift ? togglePause : togglePlay) = true;

    const float buttonsWidth = 260.0f;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - buttonsWidth) * 0.5f));

    ImGui::BeginDisabled(state == Simulation::State::Playing);
    if (ImGui::Button("Play")) simulation.Play(scene);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state != Simulation::State::Playing);
    if (ImGui::Button("Pause")) simulation.Pause();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state != Simulation::State::Paused);
    if (ImGui::Button("Step")) simulation.Step();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(state == Simulation::State::Stopped);
    if (ImGui::Button("Stop")) StopSimulation();
    ImGui::EndDisabled();

    if (togglePlay)
    {
        if (state == Simulation::State::Stopped) simulation.Play(scene); else StopSimulation();
    }
    else if (togglePause)
    {
        if (state == Simulation::State::Playing) simulation.Pause();
        else if (state == Simulation::State::Paused) simulation.Play(scene);
    }

    ImGui::SetNextItemWidth(60.0f);
    ImGui::DragFloat("##timescale", &simulation.timeScale, 0.01f, 0.0f, 10.0f, "x%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Escala del tiempo de juego");
    if (state == Simulation::State::Stopped)
        ImGui::TextDisabled("Edit");
    else
        ImGui::Text("%s  t = %.2f s  (%llu frames)", state == Simulation::State::Paused ? "Paused" : "Playing",
            simulation.GameTime(), (unsigned long long)simulation.GameFrames());
}

void Editor::StopSimulation()
{
    const uint64_t selectedUid = selectedObject ? selectedObject->uid : 0;
    selectedObject = nullptr;   // Stop sustituye todos los GameObjects
    if (!simulation.Stop(scene, resources)) return;

    // La escena restaurada tiene los mismos UIDs que antes del Play: se recupera la seleccion y el
    // historial de undo sigue valido (sus snapshots son de antes del Play).
    selectedObject = scene.FindByUid(selectedUid);
    history.Reset(scene, selectedObject);
}
