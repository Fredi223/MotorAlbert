#include "Editor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>

#include "imgui.h"
#include "Logger.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "UndoHistory.h"

namespace fs = std::filesystem;

// Panel Assets: explorador de la carpeta Assets/. A la izquierda el arbol de carpetas y a la derecha
// el contenido de la carpeta actual en una rejilla, con un icono por tipo de archivo (las texturas,
// con su miniatura). Un FBX se puede "abrir" para ver sus mallas y arrastrarlas al Inspector.

namespace
{
    // Que es cada archivo para el explorador (decide el icono, el doble clic y el menu contextual).
    enum AssetIcon { ICON_FOLDER, ICON_MODEL, ICON_MESH, ICON_TEXTURE, ICON_SCENE, ICON_UNKNOWN };

    constexpr float ICON_SIZE = 64.0f;
    constexpr float TILE_PADDING = 6.0f;

    bool IsScene(const std::string& assetPath)
    {
        return ResourceManager::PathFromUtf8(assetPath).extension() == ".scene";
    }

    AssetIcon IconOf(const Editor::AssetNode& node, const ResourceManager& resources, uint64_t& uid)
    {
        uid = 0;
        if (node.isDirectory) return ICON_FOLDER;
        if (IsScene(node.assetPath)) return ICON_SCENE;
        uid = resources.FindByAssetPath(node.assetPath);
        const ResourceManager::Entry* e = uid ? resources.Find(uid) : nullptr;
        if (!e) return ICON_UNKNOWN;   // formato no soportado o que no se pudo importar
        return e->type == ResourceType::Model ? ICON_MODEL : ICON_TEXTURE;
    }

    // a es b o esta dentro de b ("Street/x.png" esta dentro de "Street").
    bool IsSameOrInside(const std::string& a, const std::string& b)
    {
        return a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && a[b.size()] == '/');
    }

    std::string ParentOf(const std::string& assetPath)
    {
        const size_t slash = assetPath.rfind('/');
        return slash == std::string::npos ? std::string() : assetPath.substr(0, slash);
    }

    // Recorta el texto con "..." para que quepa en maxWidth.
    std::string FitText(const std::string& text, float maxWidth)
    {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
        std::string s = text;
        while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxWidth) s.pop_back();
        return s + "...";
    }

    // Rellena node con el contenido de dir: primero las carpetas y luego los archivos, por nombre. Los
    // .meta no se muestran (son del motor, no del usuario).
    void ScanDirectory(const fs::path& dir, const std::string& relPrefix, Editor::AssetNode& node)
    {
        std::error_code ec;
        for (const auto& item : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        {
            if (item.path().extension() == ".meta") continue;
            Editor::AssetNode child;
            child.name = ResourceManager::PathToUtf8(item.path().filename());
            child.assetPath = relPrefix + child.name;
            child.isDirectory = item.is_directory();
            if (child.isDirectory) ScanDirectory(item.path(), child.assetPath + "/", child);
            node.children.push_back(std::move(child));
        }
        std::sort(node.children.begin(), node.children.end(), [](const Editor::AssetNode& a, const Editor::AssetNode& b) {
            return a.isDirectory != b.isDirectory ? a.isDirectory : a.name < b.name;
        });
    }

    void CountFiles(const Editor::AssetNode& node, const ResourceManager& resources, int& files, int& imported)
    {
        for (const Editor::AssetNode& child : node.children)
        {
            if (child.isDirectory) { CountFiles(child, resources, files, imported); continue; }
            ++files;
            if (resources.FindByAssetPath(child.assetPath)) ++imported;
        }
    }

    // Iconos dibujados con formas (sin imagenes en el repo). p = esquina superior izquierda, s = lado.
    void DrawIcon(ImDrawList* dl, AssetIcon icon, ImVec2 p, float s)
    {
        const auto at = [&](float x, float y) { return ImVec2(p.x + x * s, p.y + y * s); };
        switch (icon)
        {
        case ICON_FOLDER:
            dl->AddRectFilled(at(0.08f, 0.22f), at(0.45f, 0.34f), IM_COL32(200, 160, 60, 255), 3.0f);
            dl->AddRectFilled(at(0.08f, 0.30f), at(0.92f, 0.82f), IM_COL32(230, 185, 75, 255), 4.0f);
            break;
        case ICON_MODEL:   // cubo en perspectiva isometrica
        {
            const ImVec2 top(at(0.5f, 0.12f)), left(at(0.12f, 0.32f)), right(at(0.88f, 0.32f)), mid(at(0.5f, 0.52f));
            const ImVec2 bl(at(0.12f, 0.70f)), br(at(0.88f, 0.70f)), bottom(at(0.5f, 0.90f));
            dl->AddQuadFilled(top, right, mid, left, IM_COL32(120, 175, 240, 255));
            dl->AddQuadFilled(left, mid, bottom, bl, IM_COL32(70, 120, 200, 255));
            dl->AddQuadFilled(mid, right, br, bottom, IM_COL32(45, 90, 165, 255));
            break;
        }
        case ICON_MESH:   // malla de triangulos
        {
            const ImVec2 a(at(0.15f, 0.80f)), b(at(0.50f, 0.18f)), c(at(0.85f, 0.80f)), d(at(0.50f, 0.62f));
            const ImU32 col = IM_COL32(110, 200, 120, 255);
            for (const auto& [u, v] : { std::pair{ a, b }, { b, c }, { c, a }, { a, d }, { b, d }, { c, d } })
                dl->AddLine(u, v, col, 2.0f);
            for (const ImVec2& v : { a, b, c, d }) dl->AddCircleFilled(v, 0.05f * s, col);
            break;
        }
        case ICON_SCENE:   // paisaje: cielo, sol y montanas
            dl->AddRectFilled(at(0.08f, 0.18f), at(0.92f, 0.82f), IM_COL32(90, 150, 210, 255), 4.0f);
            dl->AddCircleFilled(at(0.70f, 0.36f), 0.09f * s, IM_COL32(250, 220, 90, 255));
            dl->AddTriangleFilled(at(0.08f, 0.82f), at(0.38f, 0.40f), at(0.66f, 0.82f), IM_COL32(60, 130, 70, 255));
            dl->AddTriangleFilled(at(0.40f, 0.82f), at(0.66f, 0.52f), at(0.92f, 0.82f), IM_COL32(45, 105, 55, 255));
            break;
        case ICON_TEXTURE:   // por si la miniatura no se puede cargar: un tablero
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    dl->AddRectFilled(at(0.1f + 0.2f * x, 0.1f + 0.2f * y), at(0.3f + 0.2f * x, 0.3f + 0.2f * y),
                        (x + y) % 2 ? IM_COL32(200, 80, 200, 255) : IM_COL32(40, 40, 40, 255));
            break;
        default:   // hoja con la esquina doblada
            dl->AddQuadFilled(at(0.22f, 0.10f), at(0.62f, 0.10f), at(0.78f, 0.90f), at(0.22f, 0.90f), IM_COL32(150, 150, 150, 255));
            dl->AddTriangleFilled(at(0.62f, 0.10f), at(0.78f, 0.26f), at(0.78f, 0.90f), IM_COL32(150, 150, 150, 255));
            dl->AddTriangleFilled(at(0.62f, 0.10f), at(0.62f, 0.26f), at(0.78f, 0.26f), IM_COL32(200, 200, 200, 255));
            break;
        }
    }

    // Origen de drag & drop de un recurso hacia el Inspector (mallas y texturas) o el Viewport.
    void ResourceDragSource(const ResourceManager::Entry& e)
    {
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(Editor::DND_RESOURCE, &e.uid, sizeof(e.uid));
            ImGui::Text("%s", e.name.c_str());
            ImGui::EndDragDropSource();
        }
    }
}

void Editor::RebuildAssetTree()
{
    assetTree = AssetNode{};
    assetTree.name = "Assets";
    assetTree.isDirectory = true;
    ScanDirectory(ResourceManager::PathFromUtf8(resources.AssetsDir()), "", assetTree);
    assetTreeDirty = false;

    // La carpeta que se estaba viendo puede haber desaparecido (borrada desde Windows y Refresh).
    if (!FindAssetNode(currentFolder))
    {
        currentFolder.clear();
        thumbnails.clear();
    }
    if (!selectedAsset.empty() && !FindAssetNode(selectedAsset)) selectedAsset.clear();
}

const Editor::AssetNode* Editor::FindAssetNode(const std::string& assetPath) const
{
    const AssetNode* node = &assetTree;
    size_t start = 0;
    while (start < assetPath.size())
    {
        const size_t slash = assetPath.find('/', start);
        const std::string part = assetPath.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        const auto it = std::find_if(node->children.begin(), node->children.end(),
            [&part](const AssetNode& c) { return c.name == part; });
        if (it == node->children.end()) return nullptr;
        node = &*it;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return node;
}

std::string Editor::CurrentAssetDirectory() const
{
    const AssetNode* node = FindAssetNode(currentFolder);
    return (node && !node->isDirectory) ? ParentOf(currentFolder) : currentFolder;
}

void Editor::DrawAssetToolbar()
{
    // Subir: de un FBX a su carpeta, de una carpeta a su padre.
    ImGui::BeginDisabled(currentFolder.empty());
    if (ImGui::ArrowButton("up", ImGuiDir_Up)) { folderToOpen = ParentOf(currentFolder); changeFolder = true; }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Subir a la carpeta superior");

    // Barra de ruta: Assets > Street > Street.fbx, cada parte lleva a su carpeta.
    ImGui::SameLine();
    if (ImGui::SmallButton("Assets")) { folderToOpen.clear(); changeFolder = true; }
    size_t start = 0;
    int index = 0;
    while (start < currentFolder.size())
    {
        size_t slash = currentFolder.find('/', start);
        if (slash == std::string::npos) slash = currentFolder.size();
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushID(index++);
        if (ImGui::SmallButton(currentFolder.substr(start, slash - start).c_str()))
        {
            folderToOpen = currentFolder.substr(0, slash);
            changeFolder = true;
        }
        ImGui::PopID();
        start = slash + 1;
    }

    const float refreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - refreshWidth));
    if (ImGui::Button("Refresh"))
    {
        thumbnails.clear();
        resources.ImportAll();   // importa lo que se haya copiado a Assets/ desde fuera del motor
        assetTreeDirty = true;
    }
    ImGui::SetItemTooltip("Importa lo que se haya copiado a Assets/ desde fuera del motor");
}

void Editor::DrawFolderTree(const AssetNode& node)
{
    const bool hasSubfolders = std::any_of(node.children.begin(), node.children.end(),
        [](const AssetNode& c) { return c.isDirectory; });
    const std::string currentDir = CurrentAssetDirectory();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasSubfolders) flags |= ImGuiTreeNodeFlags_Leaf;
    if (node.assetPath == currentDir) flags |= ImGuiTreeNodeFlags_Selected;
    if (node.assetPath.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    // Al navegar desde la rejilla o la ruta, el arbol despliega los padres de la carpeta actual.
    if (revealFolder && !node.assetPath.empty() && currentDir.size() > node.assetPath.size() && IsSameOrInside(currentDir, node.assetPath))
        ImGui::SetNextItemOpen(true);

    const bool open = ImGui::TreeNodeEx(node.assetPath.empty() ? "##root" : node.assetPath.c_str(), flags, "%s", node.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        folderToOpen = node.assetPath;
        changeFolder = true;
    }
    if (!node.assetPath.empty() && ImGui::BeginPopupContextItem())
    {
        DrawAssetContextMenu(node);
        ImGui::EndPopup();
    }
    if (!open) return;
    for (const AssetNode& child : node.children)
        if (child.isDirectory) DrawFolderTree(child);
    ImGui::TreePop();
}

bool Editor::DrawAssetTile(const char* id, const std::string& label, int icon, uint64_t textureUid, bool selected)
{
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 tileSize(ICON_SIZE + TILE_PADDING * 2.0f, ICON_SIZE + lineH + TILE_PADDING * 3.0f);
    const bool clicked = ImGui::InvisibleButton(id, tileSize);
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (selected || ImGui::IsItemHovered())
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 4.0f);

    const ImVec2 iconPos(p0.x + TILE_PADDING, p0.y + TILE_PADDING);
    bool drawn = false;
    if (icon == ICON_TEXTURE && textureUid)
    {
        // La miniatura es la textura de Library (se queda en el cache mientras se vea esta carpeta).
        auto it = thumbnails.find(textureUid);
        if (it == thumbnails.end()) it = thumbnails.emplace(textureUid, resources.GetTexture(textureUid)).first;
        if (const std::shared_ptr<Texture>& tex = it->second; tex && tex->id && tex->width && tex->height)
        {
            // Con su proporcion, centrada en el cuadrado. En Library esta volteada para OpenGL (v hacia arriba).
            const float aspect = float(tex->width) / float(tex->height);
            const ImVec2 size = aspect >= 1.0f ? ImVec2(ICON_SIZE, ICON_SIZE / aspect) : ImVec2(ICON_SIZE * aspect, ICON_SIZE);
            const ImVec2 q0(iconPos.x + (ICON_SIZE - size.x) * 0.5f, iconPos.y + (ICON_SIZE - size.y) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex->id, q0, ImVec2(q0.x + size.x, q0.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
            drawn = true;
        }
    }
    if (!drawn) DrawIcon(dl, AssetIcon(icon), iconPos, ICON_SIZE);

    const std::string text = FitText(label, tileSize.x - 4.0f);
    const float textW = ImGui::CalcTextSize(text.c_str()).x;
    dl->AddText(ImVec2(p0.x + (tileSize.x - textW) * 0.5f, p0.y + ICON_SIZE + TILE_PADDING * 2.0f),
        ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
    return clicked;
}

void Editor::DrawAssetGrid()
{
    const AssetNode* folder = FindAssetNode(currentFolder);
    if (!folder) return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float tileW = ICON_SIZE + TILE_PADDING * 2.0f;
    const int columns = std::max(1, int((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (tileW + style.ItemSpacing.x)));
    int column = 0;
    const auto nextTile = [&] {
        if (++column < columns) ImGui::SameLine();
        else column = 0;
    };

    if (!folder->isDirectory)
    {
        // Dentro de un FBX: sus mallas, para arrastrarlas al campo Mesh del Inspector.
        for (const auto& [meshUid, meshEntry] : resources.Entries())
        {
            if (meshEntry.type != ResourceType::Mesh || meshEntry.assetPath != folder->assetPath) continue;
            const std::string meshName = meshEntry.name.substr(std::min(meshEntry.name.size(), folder->assetPath.size() + 1));
            ImGui::PushID(&meshEntry);
            DrawAssetTile("tile", meshName, ICON_MESH, 0, false);
            ResourceDragSource(meshEntry);
            ImGui::SetItemTooltip("%s  [Mesh]\nUID %llu\nArrastrar: al campo Mesh del Inspector",
                meshName.c_str(), (unsigned long long)meshUid);
            ImGui::PopID();
            nextTile();
        }
        return;
    }

    if (folder->children.empty()) ImGui::TextDisabled("Carpeta vacia (arrastra archivos a la ventana para importarlos aqui)");

    for (const AssetNode& node : folder->children)
    {
        uint64_t uid = 0;
        const AssetIcon icon = IconOf(node, resources, uid);
        ImGui::PushID(node.assetPath.c_str());
        if (DrawAssetTile("tile", node.name, icon, uid, node.assetPath == selectedAsset))
            selectedAsset = node.assetPath;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ActivateAsset(node);

        if (const ResourceManager::Entry* entry = uid ? resources.Find(uid) : nullptr)
            ResourceDragSource(*entry);

        switch (icon)
        {
        case ICON_FOLDER:  ImGui::SetItemTooltip("%s/\nDoble clic: abrir", node.name.c_str()); break;
        case ICON_SCENE:   ImGui::SetItemTooltip("%s  [Scene]\nDoble clic: abrir la escena (se pierden los cambios sin guardar)", node.name.c_str()); break;
        case ICON_MODEL:   ImGui::SetItemTooltip("%s  [Model]\nUID %llu\nDoble clic o arrastrar al Viewport: instanciar\nClic derecho > Show meshes: ver sus mallas",
                               node.name.c_str(), (unsigned long long)uid); break;
        case ICON_TEXTURE: ImGui::SetItemTooltip("%s  [Texture]\nUID %llu\nDoble clic: aplicar al objeto seleccionado\nArrastrar: al campo Texture del Inspector o al Viewport",
                               node.name.c_str(), (unsigned long long)uid); break;
        default:           ImGui::SetItemTooltip("%s\nNo importado (formato no soportado)", node.name.c_str()); break;
        }

        if (ImGui::BeginPopupContextItem())
        {
            selectedAsset = node.assetPath;
            DrawAssetContextMenu(node);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        nextTile();
    }
}

void Editor::ActivateAsset(const AssetNode& node)
{
    uint64_t uid = 0;
    switch (IconOf(node, resources, uid))
    {
    case ICON_FOLDER:
        folderToOpen = node.assetPath;
        changeFolder = true;
        break;
    case ICON_SCENE:
        LoadScene(node.assetPath);
        break;
    case ICON_MODEL:
        if (GameObject* model = scene.InstantiateModel(uid, resources)) selectedObject = model;
        break;
    case ICON_TEXTURE:
        AssignDroppedTexture(uid);
        break;
    default:
        break;
    }
}

void Editor::DrawAssetContextMenu(const AssetNode& node)
{
    uint64_t uid = 0;
    const AssetIcon icon = IconOf(node, resources, uid);
    switch (icon)
    {
    case ICON_FOLDER:  if (ImGui::MenuItem("Open")) ActivateAsset(node); break;
    case ICON_SCENE:   if (ImGui::MenuItem("Open")) ActivateAsset(node); break;
    case ICON_TEXTURE: if (ImGui::MenuItem("Apply to selected")) ActivateAsset(node); break;
    case ICON_MODEL:
        if (ImGui::MenuItem("Instantiate")) ActivateAsset(node);
        if (ImGui::MenuItem("Show meshes")) { folderToOpen = node.assetPath; changeFolder = true; }
        break;
    default: break;
    }

    // En el Explorador de Windows: la carpeta misma, o la que contiene el archivo.
    if (ImGui::MenuItem("Show in Explorer"))
    {
        const std::string dir = node.isDirectory ? node.assetPath : ParentOf(node.assetPath);
        const fs::path abs = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(dir);
        SDL_OpenURL(ResourceManager::PathToUtf8(abs.lexically_normal()).c_str());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete...")) RequestDeleteAsset(node);
}

void Editor::RequestDeleteAsset(const AssetNode& node)
{
    assetToDelete = node.assetPath;
    assetToDeleteFiles = assetToDeleteImported = 0;
    if (node.isDirectory) CountFiles(node, resources, assetToDeleteFiles, assetToDeleteImported);
    openDeleteAssetPopup = true;
}

void Editor::DeleteAsset(const std::string& assetPath)
{
    if (assetPath.empty()) return;   // la raiz Assets/ no se borra
    thumbnails.clear();              // sueltan sus texturas: si se borran, que se descarguen

    // Una carpeta se borra archivo a archivo (cada uno con su .meta y sus recursos de Library) y luego
    // entera, con las subcarpetas y lo que quede.
    const fs::path abs = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(assetPath);
    std::error_code ec;
    const bool isDirectory = fs::is_directory(abs, ec);
    std::vector<std::string> files;
    if (isDirectory)
    {
        for (const auto& item : fs::recursive_directory_iterator(abs, fs::directory_options::skip_permission_denied, ec))
            if (item.is_regular_file() && item.path().extension() != ".meta")
                files.push_back(resources.RelativeToAssets(item.path()));
    }
    else
        files.push_back(assetPath);

    std::vector<uint64_t> removed;
    for (const std::string& file : files)
    {
        const std::vector<uint64_t> r = resources.DeleteAsset(file);
        removed.insert(removed.end(), r.begin(), r.end());
    }
    if (isDirectory)
    {
        fs::remove_all(abs, ec);
        if (fs::exists(abs)) Console::LogError("No se pudo borrar la carpeta Assets/%s", assetPath.c_str());
        else Console::LogInfo("Carpeta borrada: Assets/%s (%zu archivos)", assetPath.c_str(), files.size());
    }

    assetTreeDirty = true;
    if (IsSameOrInside(currentScene, assetPath)) SetCurrentScene({});   // la escena abierta queda sin archivo
    if (IsSameOrInside(currentFolder, assetPath)) { currentFolder = ParentOf(assetPath); revealFolder = true; }
    if (IsSameOrInside(selectedAsset, assetPath)) selectedAsset.clear();
    if (removed.empty()) return;   // no habia recursos (p. ej. solo un .scene): nadie los usa y el undo no cambia
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
}

void Editor::DrawAssetPopups()
{
    if (openDeleteAssetPopup)
    {
        ImGui::OpenPopup("Delete asset");
        openDeleteAssetPopup = false;
    }
    if (ImGui::BeginPopupModal("Delete asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const AssetNode* node = FindAssetNode(assetToDelete);
        if (node && node->isDirectory)
        {
            ImGui::Text("Borrar la carpeta Assets/%s y todo su contenido?", assetToDelete.c_str());
            ImGui::Text("Contiene %d archivos (%d importados como recursos).", assetToDeleteFiles, assetToDeleteImported);
        }
        else
            ImGui::Text("Borrar Assets/%s?", assetToDelete.c_str());
        ImGui::TextDisabled("Se borran los archivos, sus .meta y sus recursos de Library.\n"
            "Los objetos que los usan se quedan sin ellos y el historial de undo se vacia.");
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

    if (openNewFolderPopup)
    {
        ImGui::OpenPopup("New folder");
        openNewFolderPopup = false;
        std::snprintf(newFolderBuf, sizeof(newFolderBuf), "%s", "New Folder");
    }
    if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const std::string parent = CurrentAssetDirectory();
        ImGui::Text("En Assets/%s", parent.c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", newFolderBuf, sizeof(newFolderBuf), ImGuiInputTextFlags_EnterReturnsTrue);

        const std::string name = newFolderBuf;
        const bool validName = !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
        const std::string rel = parent.empty() ? name : parent + "/" + name;
        const fs::path abs = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(rel);
        std::error_code ec;
        const bool exists = validName && fs::exists(abs, ec);
        if (!validName) ImGui::TextDisabled("Nombre no valido");
        else if (exists) ImGui::TextDisabled("Ya existe");

        ImGui::BeginDisabled(!validName || exists);
        if (ImGui::Button("Create") || (enter && validName && !exists))
        {
            if (fs::create_directory(abs, ec))
            {
                Console::LogInfo("Carpeta creada: Assets/%s", rel.c_str());
                assetTreeDirty = true;
            }
            else
                Console::LogError("No se pudo crear Assets/%s (%s)", rel.c_str(), ec.message().c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::DrawAssets()
{
    if (!showAssets) return;
    ImGui::Begin("Assets", &showAssets);

    if (assetTreeDirty) RebuildAssetTree();
    DrawAssetToolbar();

    if (ImGui::BeginTable("explorer", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("content", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("folder tree");
        DrawFolderTree(assetTree);
        ImGui::EndChild();
        revealFolder = false;

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("asset grid");
        DrawAssetGrid();
        // Clic en el hueco: quita la seleccion. Clic derecho en el hueco: menu de la carpeta.
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            selectedAsset.clear();
        if (ImGui::BeginPopupContextWindow("grid menu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("New Folder...")) openNewFolderPopup = true;
            if (ImGui::MenuItem("Refresh"))
            {
                thumbnails.clear();
                resources.ImportAll();
                assetTreeDirty = true;
            }
            if (ImGui::MenuItem("Show in Explorer"))
            {
                const fs::path abs = ResourceManager::PathFromUtf8(resources.AssetsDir()) / ResourceManager::PathFromUtf8(CurrentAssetDirectory());
                SDL_OpenURL(ResourceManager::PathToUtf8(abs.lexically_normal()).c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    DrawAssetPopups();

    // Cambio de carpeta pedido durante el recorrido: se aplica ahora. Las miniaturas de la anterior se
    // sueltan (sus texturas se descargan si nadie mas las usa).
    if (changeFolder)
    {
        changeFolder = false;
        if (folderToOpen != currentFolder)
        {
            currentFolder = folderToOpen;
            thumbnails.clear();
            selectedAsset.clear();
        }
        revealFolder = true;
    }

    ImGui::End();
}
