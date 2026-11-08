#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scene.h"

// UI de cada componente. Viven aqui (en el editor) y no en los componentes para que el motor no
// dependa de ImGui. Devuelven true si el cambio afecta al octree (hay que reconstruirlo).

static bool DrawTransform(Transform& transform)
{
    // Se edita el transform LOCAL (relativo al padre). Las matrices y AABB de mundo de el y de
    // sus hijos se recalculan antes de dibujar (Scene::UpdateTransforms).
    bool committed = false;   // el gesto de edicion (arrastre o tecleo) ha terminado

    ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.1f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3("Rotation", glm::value_ptr(transform.rotation), 0.5f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.05f))
    {
        // Escala 0 haria la matriz no invertible (el reparent la invierte). Se conserva el signo:
        // una escala negativa (espejo) es valida.
        for (int i = 0; i < 3; ++i)
            if (std::abs(transform.scale[i]) < 0.001f)
                transform.scale[i] = std::copysign(0.001f, transform.scale[i]);
    }
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Button("Reset Transform"))
    {
        transform = Transform{};
        committed = true;
    }

    // El octree solo sabe reconstruirse entero (y cada reconstruccion escribe en la consola): se
    // reconstruye una vez al terminar el gesto, no en cada frame de un arrastre.
    return committed;
}

static void DrawMesh(ComponentMesh& comp, const GameObject& go, const Renderer& renderer)
{
    if (!comp.mesh)
    {
        ImGui::TextDisabled("Sin malla asignada");
        return;
    }

    const Mesh& mesh = *comp.mesh;
    ImGui::Text("Vertices: %u", mesh.vertexCount);
    ImGui::Text("Indices: %u", mesh.indexCount);
    ImGui::Text("VAO: %u", mesh.VAO);
    ImGui::Text("Shared by: %ld objects/snapshots", comp.mesh.use_count());

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
}

static void SetTexture(ComponentMaterial& comp, GLuint tex, Scene& scene)
{
    const GLuint oldTex = comp.textureID;
    comp.textureID = tex;
    comp.texturePath.clear();
    scene.ReleaseTextureIfUnused(oldTex);
}

static void DrawMaterial(ComponentMaterial& comp, const GameObject& go, Scene& scene, const Renderer& renderer)
{
    ImGui::Text("TextureID: %u", comp.textureID);
    ImGui::TextWrapped("Path: %s", comp.texturePath.empty() ? "<none>" : comp.texturePath.c_str());

    if (comp.textureID != 0)
    {
        // El tamano se pregunta a OpenGL (nivel 0 de la textura) en vez de guardarlo, asi vale
        // para cualquier origen: FBX, drag & drop o checker.
        GLint texW = 0, texH = 0;
        glGetTextureLevelParameteriv(comp.textureID, 0, GL_TEXTURE_WIDTH, &texW);
        glGetTextureLevelParameteriv(comp.textureID, 0, GL_TEXTURE_HEIGHT, &texH);
        ImGui::Text("Size: %d x %d", texW, texH);

        ImGui::Image((ImTextureID)(intptr_t)comp.textureID, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::Button("Remove Texture"))
        {
            SetTexture(comp, 0, scene);
            Console::LogInfo("Texture removed from %s", go.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Folder") && !comp.texturePath.empty())
        {
            // Sin shell de por medio: una ruta con comillas o '&' no puede inyectar comandos.
            const std::string dir = std::filesystem::path(comp.texturePath).parent_path().string();
            SDL_OpenURL(dir.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("No texture assigned (se dibuja con la checker)");
    }

    if (ImGui::Button("Assign CheckerTexture"))
    {
        SetTexture(comp, renderer.GetCheckerTexture(), scene);
        Console::LogInfo("Checker texture asignada a %s", go.name.c_str());
    }
}

static void DrawCamera(ComponentCamera& comp)
{
    ImGui::DragFloat("FOV (vertical)", &comp.fovDeg, 0.5f, 1.0f, 179.0f, "%.1f deg");
    ImGui::DragFloat("Near", &comp.nearPlane, 0.01f, 0.001f, comp.farPlane - 0.001f, "%.3f");
    ImGui::DragFloat("Far", &comp.farPlane, 1.0f, comp.nearPlane + 0.001f, 100000.0f, "%.1f");
    ImGui::TextDisabled("(se conecta al render y al culling en la Fase 3)");
}

void Editor::DrawInspector()
{
    if (!showInspector) return;
    ImGui::Begin("Inspector");

    if (!selectedObject)
    {
        ImGui::Text("No hay ningun objeto seleccionado");
        ImGui::End();
        return;
    }

    GameObject& go = *selectedObject;
    char nameBuf[128];
    std::strncpy(nameBuf, go.name.c_str(), sizeof(nameBuf));
    nameBuf[sizeof(nameBuf) - 1] = '\0';
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
            case Component::Type::Mesh:     DrawMesh(static_cast<ComponentMesh&>(comp), go, renderer); break;
            case Component::Type::Material: DrawMaterial(static_cast<ComponentMaterial&>(comp), go, scene, renderer); break;
            case Component::Type::Camera:   DrawCamera(static_cast<ComponentCamera&>(comp)); break;
            default: break;
            }
            ImGui::EndDisabled();
            if (ImGui::SmallButton("Remove Component")) toRemove = type;
        }
        ImGui::PopID();
    }

    if (toRemove != Component::Type::Count)
    {
        // La textura del material que se quita puede ser la ultima referencia: se libera DESPUES de
        // quitarlo (ReleaseTextureIfUnused no la libera mientras algun objeto la tenga).
        const ComponentMaterial* mat = go.components.Get<ComponentMaterial>();
        const GLuint tex = (toRemove == Component::Type::Material && mat) ? mat->textureID : 0;
        go.components.Remove(toRemove);
        scene.ReleaseTextureIfUnused(tex);
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
