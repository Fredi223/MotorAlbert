#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "Logger.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "Scene.h"

// UI de cada componente. Viven aqui (en el editor) y no en los componentes para que el motor no
// dependa de ImGui. Devuelven true si el cambio afecta al octree (hay que reconstruirlo).

static bool DrawTransform(Transform& transform)
{
    // Se edita el transform LOCAL (relativo al padre). Las matrices y AABB de mundo de el y de
    // sus hijos se recalculan antes de dibujar (Scene::UpdateTransforms).
    bool changed = false;

    changed |= ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.1f);
    changed |= ImGui::DragFloat3("Rotation", glm::value_ptr(transform.rotation), 0.5f);
    if (ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.05f))
    {
        // Escala 0 haria la matriz no invertible (el reparent la invierte). Se conserva el signo:
        // una escala negativa (espejo) es valida.
        for (int i = 0; i < 3; ++i)
            if (std::abs(transform.scale[i]) < 0.001f)
                transform.scale[i] = std::copysign(0.001f, transform.scale[i]);
        changed = true;
    }
    if (ImGui::Button("Reset Transform"))
    {
        transform = Transform{};
        changed = true;
    }

    // Mientras se arrastra, el octree se reconstruye en cada frame que el objeto se mueve: si no,
    // seguiria en sus hojas antiguas y el culling o el picking lo perderian. Es barato (una pasada
    // por los objetos con malla).
    return changed;
}

// Nombre del recurso para mostrarlo en el Inspector.
static const char* ResourceName(const ResourceManager& resources, uint64_t uid)
{
    const ResourceManager::Entry* e = resources.Find(uid);
    return e ? e->name.c_str() : "<sin recurso>";
}

// Desplegable con los recursos de un tipo, que ademas acepta un recurso arrastrado (desde el panel
// Resources o Assets). Devuelve true si se ha elegido otro; chosen = su UID (0 = ninguno).
static bool ResourcePicker(const char* label, ResourceType type, uint64_t current,
    const ResourceManager& resources, uint64_t& chosen)
{
    bool picked = false;
    const char* preview = current ? ResourceName(resources, current) : "<none>";
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable("<none>", current == 0)) { chosen = 0; picked = true; }
        for (const auto& [uid, entry] : resources.Entries())
        {
            if (entry.type != type) continue;
            ImGui::PushID(&entry);
            // Los cargados en memoria se marcan con sus referencias.
            const std::string text = entry.IsLoaded()
                ? entry.name + "  [" + std::to_string(entry.RefCount()) + " refs]"
                : entry.name;
            if (ImGui::Selectable(text.c_str(), uid == current)) { chosen = uid; picked = true; }
            if (uid == current) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(Editor::DND_RESOURCE))
        {
            uint64_t uid = 0;
            std::memcpy(&uid, payload->Data, sizeof(uid));
            const ResourceManager::Entry* e = resources.Find(uid);
            if (e && e->type == type) { chosen = uid; picked = true; }
            else Console::LogError("Ese recurso no es de tipo %s", type == ResourceType::Mesh ? "malla" : "textura");
        }
        ImGui::EndDragDropTarget();
    }
    return picked && chosen != current;
}

// Devuelve true si cambia la malla (cambia el AABB: hay que reconstruir el octree).
static bool DrawMesh(ComponentMesh& comp, const GameObject& go, const Renderer& renderer, ResourceManager& resources)
{
    bool changed = false;
    uint64_t chosen = 0;
    if (ResourcePicker("Mesh", ResourceType::Mesh, comp.mesh ? comp.mesh->uid : 0, resources, chosen))
    {
        comp.mesh = chosen ? resources.GetMesh(chosen) : nullptr;   // la anterior se descarga si era su ultima referencia
        Console::LogInfo("Malla de %s: %s", go.name.c_str(), chosen ? ResourceName(resources, chosen) : "<none>");
        changed = true;
    }

    if (!comp.mesh)
    {
        ImGui::TextDisabled("Sin malla asignada");
        return changed;
    }

    const Mesh& mesh = *comp.mesh;
    ImGui::TextDisabled("UID %llu | refs: %ld", (unsigned long long)mesh.uid, comp.mesh.use_count());
    ImGui::Text("Vertices: %u | Indices: %u", mesh.vertexCount, mesh.indexCount);

    static const char* normalsModes[] = { "Use Global", "Force On", "Force Off" };
    int mode = static_cast<int>(comp.normalsMode);
    if (ImGui::Combo("Normals Mode", &mode, normalsModes, IM_ARRAYSIZE(normalsModes)))
    {
        comp.normalsMode = static_cast<ComponentMesh::NormalsMode>(mode);
        Console::LogInfo("Normales de %s: %s", go.name.c_str(), normalsModes[mode]);
    }
    ImGui::Checkbox("Face normals", &comp.showFaceNormals);
    ImGui::TextDisabled("Global Normals: %s | Face Normals: %s",
        renderer.showNormals ? "ON" : "OFF", renderer.showFaceNormals ? "ON" : "OFF");
    return changed;
}

static void DrawMaterial(ComponentMaterial& comp, const GameObject& go, ResourceManager& resources)
{
    uint64_t chosen = 0;
    if (ResourcePicker("Texture", ResourceType::Texture, comp.texture ? comp.texture->uid : 0, resources, chosen))
    {
        comp.texture = chosen ? resources.GetTexture(chosen) : nullptr;   // la anterior se descarga si era su ultima referencia
        Console::LogInfo("Textura de %s: %s", go.name.c_str(), chosen ? ResourceName(resources, chosen) : "<none>");
    }

    ImGui::ColorEdit3("Color", &comp.color.x);

    if (!comp.texture)
    {
        ImGui::TextDisabled(comp.color == glm::vec3(1.0f) ? "Sin textura y color blanco: se dibuja la checker"
                                                          : "Sin textura: se dibuja el color liso");
        return;
    }

    const Texture& tex = *comp.texture;
    ImGui::TextDisabled("UID %llu | refs: %ld | GL id %u", (unsigned long long)tex.uid, comp.texture.use_count(), tex.id);
    static const char* formats[] = { "RGBA8", "DXT1", "DXT3", "DXT5" };
    ImGui::Text("Size: %u x %u (%s)", tex.width, tex.height, formats[static_cast<int>(tex.format)]);
    ImGui::Image((ImTextureID)(intptr_t)tex.id, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
}

void Editor::DrawLensFields(ComponentCamera& lens)
{
    ImGui::DragFloat("FOV (vertical)", &lens.fovDeg, 0.5f, 1.0f, 179.0f, "%.1f deg");
    ImGui::DragFloat("Near plane", &lens.nearPlane, 0.01f, 0.001f, lens.farPlane - 0.001f, "%.3f");
    ImGui::DragFloat("Far plane", &lens.farPlane, 1.0f, lens.nearPlane + 0.001f, 100000.0f, "%.1f");
}

static void DrawCamera(ComponentCamera& comp, const GameObject& go, Scene& scene)
{
    Editor::DrawLensFields(comp);

    // Solo una culling camera a la vez: marcar esta desmarca las demas.
    if (ImGui::Checkbox("Culling camera", &comp.cullingCamera) && comp.cullingCamera)
    {
        scene.ForEach([&](GameObject& other) {
            auto* cam = other.components.Get<ComponentCamera>();
            if (cam && cam != &comp) cam->cullingCamera = false;
        });
        Console::LogInfo("%s hace ahora el frustum culling", go.name.c_str());
    }
    ImGui::TextDisabled("Mira hacia su -Z local. El aspecto es el del viewport.");
}

static void DrawRotator(ComponentRotator& comp)
{
    ImGui::DragFloat3("Deg / s (X Y Z)", &comp.degreesPerSecond.x, 1.0f, -3600.0f, 3600.0f, "%.1f");
    ImGui::TextDisabled("Solo gira durante Play (con el tiempo de juego).");
}

void Editor::DrawInspector()
{
    if (!showInspector) return;
    ImGui::Begin("Inspector");

    // Un objeto recien seleccionado (creado, instanciado, undo...) quita el asset del Inspector.
    if (selectedObject != lastInspectedObject)
    {
        lastInspectedObject = selectedObject;
        if (selectedObject) inspectAsset = false;
    }
    if (inspectAsset && !selectedAsset.empty())
    {
        DrawAssetInspector();
        ImGui::End();
        return;
    }
    // Sin asset en el Inspector: se sueltan su vista previa y sus opciones sin aplicar.
    assetPreview.reset();
    importEditFor.clear();

    if (!selectedObject)
    {
        ImGui::Text("No hay ningun objeto seleccionado");
        ImGui::End();
        return;
    }

    GameObject& go = *selectedObject;
    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", go.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        go.name = std::string(nameBuf);
    ImGui::TextDisabled("Parent: %s | Children: %zu",
        go.parent && go.parent != &scene.root ? go.parent->name.c_str() : "<scene>", go.children.size());
    ImGui::Separator();

    // El Transform es fijo: todo GameObject tiene uno, no se puede desactivar ni quitar.
    if (ImGui::CollapsingHeader("Transform (local)", ImGuiTreeNodeFlags_DefaultOpen))
        scene.structureDirty |= DrawTransform(go.transform);

    // El resto de componentes, en el orden en que se anadieron. Quitar uno se aplica al acabar el
    // bucle, para no borrar de la lista mientras se recorre.
    Component::Type toRemove = Component::Type::Count;
    for (const auto& compPtr : go.components)
    {
        Component& comp = *compPtr;
        const Component::Type type = comp.GetType();
        ImGui::PushID(static_cast<int>(type));

        // Checkbox de enabled delante de la cabecera, como en Unity.
        if (ImGui::Checkbox("##enabled", &comp.enabled))
        {
            // Una malla desactivada no se dibuja ni se puede seleccionar: el octree debe saberlo.
            if (type == Component::Type::Mesh) scene.structureDirty = true;
        }
        ImGui::SameLine();
        const bool open = ImGui::CollapsingHeader(Component::TypeName(type),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        if (ImGui::BeginPopupContextItem("component context"))
        {
            if (ImGui::MenuItem("Remove Component")) toRemove = type;
            ImGui::EndPopup();
        }

        if (open)
        {
            ImGui::BeginDisabled(!comp.enabled);
            switch (type)
            {
            case Component::Type::Mesh:     scene.structureDirty |= DrawMesh(static_cast<ComponentMesh&>(comp), go, renderer, resources); break;
            case Component::Type::Material: DrawMaterial(static_cast<ComponentMaterial&>(comp), go, resources); break;
            case Component::Type::Camera:   DrawCamera(static_cast<ComponentCamera&>(comp), go, scene); break;
            case Component::Type::Rotator:  DrawRotator(static_cast<ComponentRotator&>(comp)); break;
            default: break;
            }
            ImGui::EndDisabled();
            if (ImGui::SmallButton("Remove Component")) toRemove = type;
        }
        ImGui::PopID();
    }

    if (toRemove != Component::Type::Count)
    {
        go.components.Remove(toRemove);   // sus recursos se descargan solos si era la ultima referencia
        if (toRemove == Component::Type::Mesh) scene.structureDirty = true;
        Console::LogInfo("Componente %s quitado de %s", Component::TypeName(toRemove), go.name.c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0)))
        ImGui::OpenPopup("add component");
    if (ImGui::BeginPopup("add component"))
    {
        for (int t = 0; t < static_cast<int>(Component::Type::Count); ++t)
        {
            const auto type = static_cast<Component::Type>(t);
            if (ImGui::MenuItem(Component::TypeName(type), nullptr, false, !go.components.Has(type)))
            {
                go.components.Add(type);
                Console::LogInfo("Componente %s anadido a %s", Component::TypeName(type), go.name.c_str());
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

// --- Inspector de un asset (seleccionado en el panel Assets) ---

static const char* TextureFormatName(TextureData::Format f)
{
    switch (f)
    {
    case TextureData::Format::DXT1: return "DXT1 (DDS)";
    case TextureData::Format::DXT3: return "DXT3 (DDS)";
    case TextureData::Format::DXT5: return "DXT5 (DDS)";
    default:                        return "RGBA8";
    }
}

// Combo para un enum con valores 0..N-1.
template <class E, size_t N>
static void EnumCombo(const char* label, E& value, const char* const (&names)[N])
{
    int v = int(value);
    if (ImGui::Combo(label, &v, names, int(N))) value = E(v);
}

void Editor::DrawAssetInspector()
{
    const AssetNode* node = FindAssetNode(selectedAsset);
    if (!node)
    {
        ImGui::TextDisabled("El asset ya no existe");
        return;
    }
    const uint64_t uid = resources.FindByAssetPath(selectedAsset);
    const ResourceManager::Entry* entry = uid ? resources.Find(uid) : nullptr;

    ImGui::Text("%s", node->name.c_str());
    ImGui::TextDisabled("Assets/%s", selectedAsset.c_str());
    ImGui::Separator();

    if (node->isDirectory)
    {
        ImGui::Text("Carpeta (%zu elementos)", node->children.size());
        return;
    }
    if (!entry)
    {
        ImGui::Text(ResourceManager::PathFromUtf8(selectedAsset).extension() == ".scene" ? "Escena (.scene, JSON)" : "No importado (formato no soportado)");
        return;
    }
    ImGui::TextDisabled("UID %llu", (unsigned long long)uid);

    if (entry->type == ResourceType::Model)
    {
        int meshCount = 0;
        for (const auto& [meshUid, e] : resources.Entries())
            if (e.type == ResourceType::Mesh && e.assetPath == selectedAsset) ++meshCount;
        ImGui::Text("Modelo (FBX): %d mallas", meshCount);
        ImGui::TextDisabled("Clic derecho > Show meshes en Assets para verlas");
        return;
    }

    // --- Textura ---
    // Las opciones se leen del .meta al seleccionarla; se editan aqui y solo se aplican con Apply.
    if (importEditFor != selectedAsset)
    {
        importEditFor = selectedAsset;
        textureImportSaved = textureImportEdit = resources.GetTextureImportOptions(selectedAsset);
        assetPreview = resources.GetTexture(uid);
    }

    if (assetPreview)
    {
        ImGui::Text("Size: %ux%u | Format: %s", assetPreview->width, assetPreview->height, TextureFormatName(assetPreview->format));
        ImGui::TextDisabled("refs: %ld (sin contar la vista previa)", entry->RefCount() - 1);

        // Vista previa con su proporcion, al ancho del panel (como mucho 256 px).
        const float side = std::min(256.0f, ImGui::GetContentRegionAvail().x);
        const float aspect = float(assetPreview->width) / float(std::max(1u, assetPreview->height));
        const ImVec2 size = aspect >= 1.0f ? ImVec2(side, side / aspect) : ImVec2(side * aspect, side);
        ImGui::Image((ImTextureID)(intptr_t)assetPreview->id, size, ImVec2(0, 1), ImVec2(1, 0));
    }
    else
        ImGui::TextDisabled("No se pudo cargar la textura");

    if (!ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static constexpr const char* filters[] = { "Linear + mipmaps", "Linear", "Nearest" };
    static constexpr const char* wraps[] = { "Repeat", "Clamp to edge", "Mirrored repeat" };
    TextureImportOptions& o = textureImportEdit;
    EnumCombo("Filter", o.sampler.filter, filters);
    EnumCombo("Wrap S", o.sampler.wrapS, wraps);
    EnumCombo("Wrap T", o.sampler.wrapT, wraps);

    // En los DXT solo se puede voltear en Y (DdsLoader ya invierte las filas de los bloques).
    const bool compressed = assetPreview && assetPreview->format != TextureData::Format::RGBA8;
    ImGui::BeginDisabled(compressed);
    ImGui::Checkbox("Flip X", &o.flipX);
    ImGui::EndDisabled();
    if (compressed) ImGui::SetItemTooltip("Los DDS comprimidos (DXT) no se voltean en horizontal");
    ImGui::SameLine();
    ImGui::Checkbox("Flip Y", &o.flipY);
    ImGui::TextDisabled("Se guardan en el .meta. El flip cambia los pixeles: Apply reimporta el .tex.");

    const bool dirty = !(o == textureImportSaved);
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Apply"))
    {
        if (resources.ApplyTextureImportOptions(selectedAsset, o))
        {
            textureImportSaved = o;
            Console::LogInfo("Import Settings aplicadas: %s", selectedAsset.c_str());
        }
        else
            Console::LogError("No se pudieron aplicar las Import Settings de %s", selectedAsset.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) o = textureImportSaved;
    ImGui::EndDisabled();
    if (dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Cambios sin aplicar");
    }
}
