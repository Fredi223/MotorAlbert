#include "Editor.h"

#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "Logger.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Simulation.h"
#include "UndoHistory.h"

namespace fs = std::filesystem;

static constexpr const char* SCENES_DIR = "Scenes";
static constexpr const char* SCENE_EXT = ".scene";

void Editor::SetCurrentScene(const std::string& assetPath)
{
    currentScene = assetPath;
    const std::string title = "Motor Albert - " + (assetPath.empty() ? std::string("(sin guardar)") : assetPath);
    SDL_SetWindowTitle(window, title.c_str());
}

void Editor::NewScene()
{
    if (!simulation.IsStopped()) { Console::LogError("Para la simulacion (Stop) antes de crear una escena nueva"); return; }
    selectedObject = nullptr;
    scene.Clear();
    scene.RebuildOctree();   // el render de este frame no debe ver los punteros de la escena anterior
    history.Clear();
    history.Reset(scene, selectedObject);
    SetCurrentScene({});
    Console::LogInfo("Escena nueva");
}

bool Editor::SaveScene(const std::string& assetPath)
{
    // En Play se guardaria el estado de la simulacion, no la escena que se esta editando.
    if (!simulation.IsStopped()) { Console::LogError("Para la simulacion (Stop) antes de guardar la escena"); return false; }
    const fs::path file = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(assetPath);
    if (!SceneSerializer::SaveToFile(scene, file)) return false;
    SetCurrentScene(assetPath);
    assetTreeDirty = true;   // el archivo puede ser nuevo
    Console::LogInfo("Escena guardada: Assets/%s", assetPath.c_str());
    return true;
}

bool Editor::LoadScene(const std::string& assetPath)
{
    if (!simulation.IsStopped()) { Console::LogError("Para la simulacion (Stop) antes de abrir otra escena"); return false; }
    const fs::path file = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(assetPath);
    selectedObject = nullptr;   // si falla la escena no cambia, pero sin seleccion nunca hay un puntero colgando
    if (!SceneSerializer::LoadFromFile(file, scene, resources)) return false;

    // Los snapshots son de la escena anterior: se sueltan (y con ellos sus recursos) y se empieza de cero.
    history.Clear();
    history.Reset(scene, selectedObject);
    SetCurrentScene(assetPath);
    size_t count = 0;
    scene.ForEach([&count](const GameObject&) { ++count; });
    Console::LogInfo("Escena cargada: Assets/%s (%zu GameObjects)", assetPath.c_str(), count);
    return true;
}

// Popups de "Save Scene As" (pide un nombre) y "Open Scene" (lista los .scene de Assets/Scenes/).
void Editor::DrawSceneFilePopups()
{
    if (openSaveScenePopup)
    {
        openSaveScenePopup = false;
        const std::string stem = ResourceManager::PathToUtf8(ResourceManager::PathFromUtf8(currentScene).stem());
        snprintf(sceneNameBuf, sizeof(sceneNameBuf), "%s", stem.empty() ? "NewScene" : stem.c_str());
        ImGui::OpenPopup("Save Scene As");
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Se guarda en Assets/%s/<nombre>%s", SCENES_DIR, SCENE_EXT);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Nombre", sceneNameBuf, sizeof(sceneNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        // Solo un nombre de archivo: nada de carpetas ni caracteres que Windows no admite.
        const std::string name = sceneNameBuf;
        const bool valid = !name.empty() && name.find_first_of("\\/:*?\"<>|") == std::string::npos;
        const std::string assetPath = std::string(SCENES_DIR) + "/" + name + SCENE_EXT;
        const bool exists = valid && fs::exists(ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(assetPath));
        if (!valid) ImGui::TextDisabled("Nombre no valido");
        else if (exists) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Ya existe: se sobrescribira");

        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Save") || (enter && valid))
        {
            SaveScene(assetPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (openLoadScenePopup)
    {
        openLoadScenePopup = false;
        ImGui::OpenPopup("Open Scene");
    }
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // La carpeta es pequena: se lista en cada frame que el popup esta abierto.
        std::vector<std::string> scenes;
        std::error_code ec;
        const fs::path dir = ResourceManager::PathFromUtf8(resources.AssetsDir()) / SCENES_DIR;
        for (const auto& item : fs::directory_iterator(dir, ec))
            if (item.is_regular_file() && item.path().extension() == SCENE_EXT)
                scenes.push_back(std::string(SCENES_DIR) + "/" + ResourceManager::PathToUtf8(item.path().filename()));
        std::sort(scenes.begin(), scenes.end());

        if (scenes.empty()) ImGui::TextDisabled("No hay escenas en Assets/%s/", SCENES_DIR);
        ImGui::TextDisabled("Los cambios sin guardar de la escena actual se pierden.");
        for (const std::string& s : scenes)
        {
            if (ImGui::Selectable(s.c_str(), s == currentScene))
            {
                LoadScene(s);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
