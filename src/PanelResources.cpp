#include "Editor.h"

#include "imgui.h"
#include "ResourceManager.h"

// Panel Resources: todos los recursos que conoce el ResourceManager, con sus referencias. Un recurso
// esta en memoria mientras tenga alguna (GameObjects de la escena y snapshots del undo); con 0 se
// ha descargado y se volvera a leer de Library al pedirlo. Cada fila se puede arrastrar a los campos
// Mesh o Texture del Inspector.
void Editor::DrawResources()
{
    if (!showResources) return;
    ImGui::Begin("Resources", &showResources);

    size_t meshes = 0, textures = 0, models = 0, loadedMeshes = 0, loadedTextures = 0;
    for (const auto& [uid, e] : resources.Entries())
    {
        switch (e.type)
        {
        case ResourceType::Mesh:    ++meshes; loadedMeshes += e.IsLoaded(); break;
        case ResourceType::Texture: ++textures; loadedTextures += e.IsLoaded(); break;
        case ResourceType::Model:   ++models; break;
        }
    }
    ImGui::Text("En memoria: %zu/%zu mallas, %zu/%zu texturas | %zu modelos importados",
        loadedMeshes, meshes, loadedTextures, textures, models);
    ImGui::Checkbox("Solo los cargados", &onlyLoadedResources);
    ImGui::SameLine();
    ImGui::TextDisabled("(refs = GameObjects + snapshots del undo que lo usan)");

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("resources", 5, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Refs");
        ImGui::TableSetupColumn("UID");
        ImGui::TableSetupColumn("Library");
        ImGui::TableHeadersRow();

        for (const auto& [uid, e] : resources.Entries())
        {
            // Los modelos no se quedan en memoria (solo se leen al instanciarlos): no tienen referencias.
            const bool isModel = e.type == ResourceType::Model;
            if (onlyLoadedResources && !e.IsLoaded()) continue;

            ImGui::TableNextRow();
            ImGui::PushID(&e);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(isModel ? "Model" : e.type == ResourceType::Mesh ? "Mesh" : "Texture");

            ImGui::TableNextColumn();
            if (e.IsLoaded()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
            ImGui::Selectable(e.name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (e.IsLoaded()) ImGui::PopStyleColor();
            if (!isModel && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(DND_RESOURCE, &e.uid, sizeof(e.uid));
                ImGui::Text("%s", e.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered() && !e.assetPath.empty())
                ImGui::SetTooltip("Asset: Assets/%s\nLibrary: %s", e.assetPath.c_str(), e.libraryPath.c_str());

            ImGui::TableNextColumn();
            if (isModel) ImGui::TextDisabled("-");
            else if (e.IsLoaded()) ImGui::Text("%ld", e.RefCount());
            else ImGui::TextDisabled("0");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%llu", (unsigned long long)e.uid);

            ImGui::TableNextColumn();
            ImGui::TextDisabled(e.assetPath.empty() ? "built-in" : "importado");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}
