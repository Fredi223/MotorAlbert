#include "Editor.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "imgui.h"
#include "Logger.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "UndoHistory.h"

namespace fs = std::filesystem;

// Rellena node con el contenido de dir: primero las carpetas y luego los archivos, por nombre. Los
// .meta no se muestran (son del motor, no del usuario).
static void ScanDirectory(const fs::path& dir, const std::string& relPrefix, Editor::AssetNode& node)
{
    std::error_code ec;
    for (const auto& item : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
    {
        const std::string name = ResourceManager::PathToUtf8(item.path().filename());
        if (item.path().extension() == ".meta") continue;

        Editor::AssetNode child;
        child.name = name;
        child.assetPath = relPrefix + name;
        child.isDirectory = item.is_directory();
        if (child.isDirectory) ScanDirectory(item.path(), child.assetPath + "/", child);
        node.children.push_back(std::move(child));
    }
    std::sort(node.children.begin(), node.children.end(), [](const Editor::AssetNode& a, const Editor::AssetNode& b) {
        return a.isDirectory != b.isDirectory ? a.isDirectory : a.name < b.name;
    });
}

void Editor::RebuildAssetTree()
{
    assetTree = AssetNode{};
    assetTree.name = "Assets";
    assetTree.isDirectory = true;
    ScanDirectory(ResourceManager::PathFromUtf8(resources.AssetsDir()), "", assetTree);
    assetTreeDirty = false;
}

// Origen de drag & drop de un recurso (malla o textura) hacia los campos del Inspector.
static void ResourceDragSource(const ResourceManager::Entry& e)
{
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(Editor::DND_RESOURCE, &e.uid, sizeof(e.uid));
        ImGui::Text("%s", e.name.c_str());
        ImGui::EndDragDropSource();
    }
}

void Editor::DrawAssetNode(const AssetNode& node)
{
    if (node.isDirectory)
    {
        if (ImGui::TreeNodeEx(node.assetPath.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s/", node.name.c_str()))
        {
            for (const AssetNode& child : node.children) DrawAssetNode(child);
            ImGui::TreePop();
        }
        return;
    }

    const uint64_t uid = resources.FindByAssetPath(node.assetPath);
    const ResourceManager::Entry* entry = uid ? resources.Find(uid) : nullptr;
    if (!entry)
    {
        // Formato no soportado (o que no se pudo importar): se ve, pero no se puede usar.
        ImGui::TreeNodeEx(node.assetPath.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth, "%s", node.name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("No importado (formato no soportado)");
        return;
    }

    const bool isModel = entry->type == ResourceType::Model;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!isModel) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool open = ImGui::TreeNodeEx(node.assetPath.c_str(), flags, "%s  [%s]", node.name.c_str(), isModel ? "Model" : "Texture");

    if (!isModel) ResourceDragSource(*entry);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("UID %llu\n%s", (unsigned long long)uid,
            isModel ? "Doble clic: instanciar en la escena" : "Doble clic: aplicar al objeto seleccionado\nArrastrar: al campo Texture del Inspector");
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (!isModel) AssignDroppedTexture(uid);
            else if (GameObject* model = scene.InstantiateModel(uid, resources)) selectedObject = model;
        }
    }
    if (ImGui::BeginPopupContextItem())
    {
        if (isModel && ImGui::MenuItem("Instantiate"))
        {
            if (GameObject* model = scene.InstantiateModel(uid, resources)) selectedObject = model;
        }
        if (!isModel && ImGui::MenuItem("Apply to selected")) AssignDroppedTexture(uid);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete..."))
        {
            assetToDelete = node.assetPath;
            openDeleteAssetPopup = true;
        }
        ImGui::EndPopup();
    }

    // Un modelo se despliega con sus mallas, que se pueden arrastrar al campo Mesh del Inspector.
    if (isModel && open)
    {
        for (const auto& [meshUid, meshEntry] : resources.Entries())
        {
            if (meshEntry.type != ResourceType::Mesh || meshEntry.assetPath != node.assetPath) continue;
            const std::string meshName = meshEntry.name.substr(std::min(meshEntry.name.size(), node.assetPath.size() + 1));
            ImGui::TreeNodeEx(&meshEntry, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth, "%s  [Mesh]", meshName.c_str());
            ResourceDragSource(meshEntry);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("UID %llu\nArrastrar: al campo Mesh del Inspector", (unsigned long long)meshUid);
        }
        ImGui::TreePop();
    }
}

void Editor::DeleteAsset(const std::string& assetPath)
{
    const std::vector<uint64_t> removed = resources.DeleteAsset(assetPath);
    const std::set<uint64_t> gone(removed.begin(), removed.end());

    // Los GameObjects que usaban sus mallas o texturas se quedan sin ellas (la checker / sin malla).
    int unhooked = 0;
    scene.ForEach([&](GameObject& go) {
        if (auto* mesh = go.components.Get<ComponentMesh>(); mesh && mesh->mesh && gone.count(mesh->mesh->uid))
        {
            mesh->mesh.reset();
            ++unhooked;
        }
        if (auto* mat = go.components.Get<ComponentMaterial>(); mat && mat->texture && gone.count(mat->texture->uid))
        {
            mat->texture.reset();
            ++unhooked;
        }
    });
    scene.structureDirty = true;   // las mallas quitadas ya no cuentan para el octree

    // Los snapshots tambien retienen esos recursos: sin el historial se descargan del todo.
    history.Clear();
    history.Reset(scene, selectedObject);
    if (unhooked) Console::LogInfo("%d componentes se han quedado sin su recurso borrado", unhooked);
    Console::LogInfo("Historial de undo vaciado (borrar un asset no se puede deshacer)");
    assetTreeDirty = true;
}

void Editor::DrawAssets()
{
    if (!showAssets) return;
    ImGui::Begin("Assets", &showAssets);

    if (ImGui::Button("Refresh"))
    {
        resources.ImportAll();   // importa lo que se haya copiado a Assets/ desde fuera del motor
        assetTreeDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) SDL_OpenURL(resources.AssetsDir().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Arrastra archivos a la ventana para importarlos");

    if (assetTreeDirty) RebuildAssetTree();

    ImGui::BeginChild("asset tree");
    for (const AssetNode& child : assetTree.children) DrawAssetNode(child);
    ImGui::EndChild();

    // Confirmacion de borrado. Se aplica aqui, fuera del recorrido del arbol (que se reconstruye).
    if (openDeleteAssetPopup)
    {
        ImGui::OpenPopup("Delete asset");
        openDeleteAssetPopup = false;
    }
    if (ImGui::BeginPopupModal("Delete asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Borrar Assets/%s?", assetToDelete.c_str());
        ImGui::TextDisabled("Se borran el archivo, su .meta y sus recursos de Library.\n"
            "Los objetos que lo usan se quedan sin el y el historial de undo se vacia.");
        if (ImGui::Button("Delete"))
        {
            DeleteAsset(assetToDelete);
            assetToDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            assetToDelete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
