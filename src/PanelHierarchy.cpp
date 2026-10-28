#include "Editor.h"

#include <cstring>

#include "imgui.h"
#include "Logger.h"
#include "Scene.h"

void Editor::DrawHierarchy()
{
    // -------------------- Hierarchy panel (drag/rename/dup/delete) --------------------
    // Oculto desde View > Hierarchy. El undo y la reconstruccion del octree no dependen de este
    // panel (van en DrawFrame), asi que siguen funcionando aunque no se dibuje.
    if (!showHierarchy) return;

    ImGui::Begin("Hierarchy", &showHierarchy);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (selectedObject >= 0 && selectedObject < (int)scene.objects.size()) {
                scene.Erase(selectedObject, selectedObject);
            }
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            if (selectedObject >= 0 && selectedObject < (int)scene.objects.size()) {
                int newIdx = scene.Duplicate(selectedObject);
                if (newIdx != -1) selectedObject = newIdx;
            }
        }
    }

    for (int i = 0; i < (int)scene.objects.size(); ++i)
    {
        ImGui::PushID(i);

        ImVec4 textColor = scene.objects[i].hidden ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);

        bool isSelected = (selectedObject == i);
        if (ImGui::Selectable(scene.objects[i].name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectedObject = i;
            if (ImGui::IsMouseDoubleClicked(0)) {
                renameTargetIdx = i;
                std::strncpy(renameBuf, scene.objects[i].name.c_str(), sizeof(renameBuf) - 1);
                renameBuf[sizeof(renameBuf) - 1] = '\0';
                ImGui::OpenPopup("Rename Popup");
            }
        }
        if (ImGui::BeginPopupContextItem(nullptr))
        {
            if (ImGui::MenuItem("Rename")) {
                renameTargetIdx = i;
                std::strncpy(renameBuf, scene.objects[i].name.c_str(), sizeof(renameBuf) - 1);
                renameBuf[sizeof(renameBuf) - 1] = '\0';
                ImGui::OpenPopup("Rename Popup");
            }
            if (ImGui::MenuItem("Duplicate")) {
                int newIdx = scene.Duplicate(i);
                if (newIdx != -1) {
                    selectedObject = newIdx;
                    Console::LogInfo("Duplicated %s -> %s", scene.objects[i].name.c_str(), scene.objects[newIdx].name.c_str());
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
            ImGui::Text("%s", scene.objects[i].name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_GAMEOBJECT"))
            {
                int src = *(const int*)payload->Data;
                int dst = i;
                if (src != dst && src >= 0 && src < (int)scene.objects.size())
                {
                    GameObject tmp = std::move(scene.objects[src]);
                    scene.objects.erase(scene.objects.begin() + src);
                    scene.objects.insert(scene.objects.begin() + dst, std::move(tmp));
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

    if (ImGui::BeginPopupModal("Rename Popup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (renameTargetIdx >= 0 && renameTargetIdx < (int)scene.objects.size())
        {
            ImGui::Text("Rename object:");
            ImGui::InputText("##rename", renameBuf, IM_ARRAYSIZE(renameBuf));
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                scene.objects[renameTargetIdx].name = std::string(renameBuf);
                ImGui::CloseCurrentPopup();
                renameTargetIdx = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
                renameTargetIdx = -1;
            }
        }
        else
        {
            ImGui::Text("Target no valido");
            if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); renameTargetIdx = -1; }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
