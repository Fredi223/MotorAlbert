#include "Editor.h"

#include <algorithm>
#include <cctype>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"   // DockBuilder

#include "DdsLoader.h"
#include "FrameTiming.h"
#include "Logger.h"
#include "Primitives.h"
#include "Renderer.h"
#include "Scene.h"
#include "TextureLoader.h"
#include "UndoHistory.h"

Editor::Editor(SDL_Window* window, Scene& scene, UndoHistory& history, Renderer& renderer, FrameTiming& timing)
    : window(window), scene(scene), history(history), renderer(renderer), timing(timing)
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
            if (selectedObject >= 0 && selectedObject < static_cast<int>(scene.objects.size()))
            {
                scene.objects[selectedObject].hidden = !scene.objects[selectedObject].hidden;

                if (scene.objects[selectedObject].hidden)
                {
                    Console::LogInfo("%s ocultado", scene.objects[selectedObject].name.c_str());
                    selectedObject = -1;
                }
                else
                {
                    Console::LogInfo("%s mostrado", scene.objects[selectedObject].name.c_str());
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

    drawnCount = renderer.Render(scene, view, projection);

    Console::DrawConsole("Console", &showConsole);
    DrawHierarchy();

    // Ctrl+Z / Ctrl+Y (o Ctrl+Shift+Z). Va antes de reconstruir el octree porque deshacer
    // sustituye la escena entera.
    if (io.KeyCtrl && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) { if (io.KeyShift) history.RequestRedo(); else history.RequestUndo(); }
        else if (ImGui::IsKeyPressed(ImGuiKey_Y)) history.RequestRedo();
    }
    history.Update(scene, selectedObject);

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
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit")) quitRequested = true;
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
        ImGui::DockBuilderDockWindow("Console", dock_bottom);        // abajo
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

// Aplica una textura recien cargada (PNG/JPG/TGA/DDS soltada en la ventana) al objeto
// seleccionado, o al primero si no hay seleccion. Si no hay objetos, la libera.
void Editor::AssignDroppedTexture(GLuint newTex, const std::string& filePath)
{
    int target = -1;
    if (selectedObject >= 0 && selectedObject < (int)scene.objects.size()) target = selectedObject;
    else if (!scene.objects.empty()) target = 0;

    if (target < 0)
    {
        glDeleteTextures(1, &newTex);   // nadie la va a usar
        Console::LogInfo("No hay GameObjects para aplicar la textura");
        return;
    }
    GameObject& go = scene.objects[target];
    const GLuint oldTex = go.texComp.textureID;
    go.texComp.textureID = newTex;
    go.texComp.path = filePath;
    scene.ReleaseTextureIfUnused(oldTex);
    Console::LogInfo("Textura aplicada a: %s (path: %s)", go.name.c_str(), filePath.c_str());
}
