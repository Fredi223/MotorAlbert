#include "Editor.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "Logger.h"
#include "Scene.h"

static constexpr const char* DND_GAMEOBJECT = "DND_GAMEOBJECT";   // payload: uint64_t con el UID

void Editor::BeginRename(const GameObject& go)
{
    renameTargetUid = go.uid;
    std::strncpy(renameBuf, go.name.c_str(), sizeof(renameBuf) - 1);
    renameBuf[sizeof(renameBuf) - 1] = '\0';
    openRenamePopup = true;   // el popup se abre fuera del arbol: dentro de un TreeNode cambia la pila de IDs
}

void Editor::DeleteObject(GameObject* go)
{
    if (!go) return;
    if (selectedObject && (selectedObject == go || selectedObject->IsDescendantOf(go)))
        selectedObject = nullptr;
    Console::LogInfo("Borrado %s (con sus hijos)", go->name.c_str());
    scene.Erase(go);
}

void Editor::DrawHierarchyNode(GameObject& go, HierarchyOps& ops)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (go.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selectedObject == &go) flags |= ImGuiTreeNodeFlags_Selected;

    // Al seleccionar desde el viewport, se despliegan los padres para que la seleccion se vea.
    if (revealSelection && selectedObject && selectedObject->IsDescendantOf(&go))
        ImGui::SetNextItemOpen(true);

    const ImVec4 textColor = go.IsHiddenInHierarchy() ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(go.uid)), flags, "%s", go.name.c_str());
    ImGui::PopStyleColor();

    if (revealSelection && selectedObject == &go)
        ImGui::SetScrollHereY();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        selectedObject = &go;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) BeginRename(go);
    }

    if (ImGui::BeginPopupContextItem())
    {
        selectedObject = &go;
        if (ImGui::MenuItem("Rename")) BeginRename(go);
        if (ImGui::MenuItem("Create Empty Child")) ops.createChildOf = &go;
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) ops.toDuplicate = &go;
        if (ImGui::MenuItem("Unparent", nullptr, false, go.parent != &scene.root))
        {
            ops.reparentUid = go.uid;
            ops.reparentTo = nullptr;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Supr")) ops.toDelete = &go;
        ImGui::EndPopup();
    }

    // Arrastrar un objeto sobre otro lo convierte en su hijo (conservando su posicion en el mundo).
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload(DND_GAMEOBJECT, &go.uid, sizeof(go.uid));   // ImGui copia el payload
        ImGui::Text("%s", go.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_GAMEOBJECT))
        {
            ops.reparentUid = *static_cast<const uint64_t*>(payload->Data);
            ops.reparentTo = &go;
        }
        ImGui::EndDragDropTarget();
    }

    if (open)
    {
        for (const auto& child : go.children)
            DrawHierarchyNode(*child, ops);
        ImGui::TreePop();
    }
}

void Editor::DrawHierarchy()
{
    // Oculto desde View > Hierarchy. El undo y la reconstruccion del octree no dependen de este
    // panel (van en DrawFrame), asi que siguen funcionando aunque no se dibuje.
    if (!showHierarchy)
    {
        revealSelection = false;
        return;
    }

    ImGui::Begin("Hierarchy", &showHierarchy);

    HierarchyOps ops;

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) ops.toDelete = selectedObject;
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) ops.toDuplicate = selectedObject;
    }

    for (const auto& child : scene.root.children)
        DrawHierarchyNode(*child, ops);
    revealSelection = false;

    // Hueco libre bajo el arbol: soltar ahi un objeto lo lleva al primer nivel, un click quita la
    // seleccion y el click derecho permite crear un objeto vacio.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##HierarchyEmptyArea", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 40.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) selectedObject = nullptr;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DND_GAMEOBJECT))
        {
            ops.reparentUid = *static_cast<const uint64_t*>(payload->Data);
            ops.reparentTo = nullptr;
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem("##HierarchyEmptyContext"))
    {
        if (ImGui::MenuItem("Create Empty")) selectedObject = scene.CreateEmpty("GameObject");
        ImGui::EndPopup();
    }

    // --- Aplicar lo pedido durante el recorrido ---
    if (ops.reparentUid != 0)
    {
        if (GameObject* child = scene.FindByUid(ops.reparentUid))
        {
            if (scene.Reparent(child, ops.reparentTo))
                Console::LogInfo("%s ahora es hijo de %s", child->name.c_str(),
                    ops.reparentTo ? ops.reparentTo->name.c_str() : "la escena (primer nivel)");
            else if (child->parent != (ops.reparentTo ? ops.reparentTo : &scene.root))
                Console::LogError("No se puede mover %s dentro de si mismo o de uno de sus hijos", child->name.c_str());
        }
    }
    if (ops.toDuplicate)
    {
        GameObject* copy = scene.Duplicate(ops.toDuplicate);
        Console::LogInfo("Duplicado %s -> %s", ops.toDuplicate->name.c_str(), copy->name.c_str());
        selectedObject = copy;
    }
    if (ops.createChildOf)
    {
        selectedObject = scene.CreateEmpty("GameObject", ops.createChildOf);
        revealSelection = true;
    }
    if (ops.toDelete) DeleteObject(ops.toDelete);

    // --- Renombrar ---
    if (openRenamePopup)
    {
        ImGui::OpenPopup("Rename Popup");
        openRenamePopup = false;
    }
    if (ImGui::BeginPopupModal("Rename Popup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (GameObject* target = scene.FindByUid(renameTargetUid))
        {
            ImGui::Text("Rename object:");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##rename", renameBuf, IM_ARRAYSIZE(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)) || enter)
            {
                target->name = std::string(renameBuf);
                ImGui::CloseCurrentPopup();
                renameTargetUid = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                renameTargetUid = 0;
            }
        }
        else
        {
            ImGui::Text("El objeto ya no existe");
            if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); renameTargetUid = 0; }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
