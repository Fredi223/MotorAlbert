#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "imgui.h"
#include "Geometry.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scene.h"

void Editor::DrawViewport()
{
    ImGui::Begin("Viewport");
    const RenderStats& rs = renderStats;
    ImGui::TextColored(
        renderer.frustumCulling ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
        "Frustum Culling: %s", renderer.frustumCulling ? "ON" : "OFF");
    ImGui::SameLine();
    ImGui::TextDisabled("(camera: %s)", rs.cullingCamera.empty() ? "editor" : rs.cullingCamera.c_str());
    ImGui::Text("Drawn: %d | Culled: %d | Octree: %d candidates, %d of %d nodes visited",
        rs.drawn, rs.culled, rs.candidates, rs.nodesVisited, rs.nodesTotal);
    ImGui::Separator();

    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 mousePos = ImGui::GetMousePos();

    relMouseX = mousePos.x - cursorPos.x;
    relMouseY = mousePos.y - cursorPos.y;
    mouseInsideViewport =
        (relMouseX >= 0 && relMouseX < size.x &&
            relMouseY >= 0 && relMouseY < size.y);

    // Minimo 1 px: con el panel colapsado o minimizado size puede ser 0, y un ancho/alto 0
    // daria un framebuffer vacio y una proyeccion con aspect = x/0 (infinito o NaN).
    const int newViewportW = std::max(1, (int)size.x);
    const int newViewportH = std::max(1, (int)size.y);
    if (newViewportW != renderer.GetViewportWidth() || newViewportH != renderer.GetViewportHeight())
    {
        viewportWidthF = (float)newViewportW;
        viewportHeightF = (float)newViewportH;
        renderer.ResizeViewport(newViewportW, newViewportH);
    }

    // ========================================================
    // SELECCION DE OBJETOS (RAYCAST: OCTREE -> AABB -> TRIANGULOS)
    // ========================================================
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !altHeld && mouseInsideViewport && !ImGui::IsAnyItemHovered())
    {
        // relMouseX/Y ya son relativos al viewport (origen arriba-izq), tal como los espera la funcion
        const glm::vec3 rayDir = ScreenToWorldRay_UnProject(relMouseX, relMouseY,
            viewportWidthF, viewportHeightF, projection, view);
        const glm::vec3 rayOrigin = camera.position;
        renderer.SetPickRay(rayOrigin, rayOrigin + rayDir * camera.lens.farPlane);

        if (GameObject* hit = PickObject(rayOrigin, rayDir))
        {
            selectedObject = hit;
            revealSelection = true;   // la Hierarchy despliega sus padres
            Console::LogInfo("Seleccionado: %s", hit->name.c_str());
        }
        else
        {
            selectedObject = nullptr;
            Console::LogInfo("Seleccion: ninguno");
        }
    }

    ImGui::Image((ImTextureID)(intptr_t)renderer.GetViewportTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End(); // cierra Viewport
}

GameObject* Editor::PickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDir) const
{
    // 1) El octree descarta los nodos que el rayo no atraviesa.
    std::vector<GameObject*> candidates;
    scene.octree.QueryRay(rayOrigin, rayDir, candidates);

    // 2) De los candidatos, los que el rayo toca en su AABB, ordenados por la distancia de entrada.
    std::vector<std::pair<float, GameObject*>> hits;
    for (GameObject* go : candidates)
    {
        if (go->IsHiddenInHierarchy() || !go->HasMesh()) continue;
        float tEnter = 0.0f;
        if (RayIntersectsAABB(rayOrigin, rayDir, go->worldAabbMin, go->worldAabbMax, tEnter))
            hits.emplace_back(tEnter, go);
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // 3) Test fino contra los triangulos, en el espacio local de cada malla (el rayo se lleva alli con
    //    la inversa de su matriz de mundo; la t sale en las mismas unidades que en el rayo de mundo).
    //    Como estan ordenados, en cuanto un AABB empieza mas lejos que el mejor impacto ya no hace
    //    falta seguir: nada de lo que queda puede estar delante.
    float best = std::numeric_limits<float>::max();
    GameObject* hit = nullptr;
    for (const auto& [tEnter, go] : hits)
    {
        if (tEnter > best) break;
        const glm::mat4 toLocal = glm::inverse(go->worldMatrix);
        const glm::vec3 localOrigin = glm::vec3(toLocal * glm::vec4(rayOrigin, 1.0f));
        const glm::vec3 localDir = glm::vec3(toLocal * glm::vec4(rayDir, 0.0f));
        float t = 0.0f;
        if (RayIntersectsMesh(*go->ActiveMesh()->mesh, localOrigin, localDir, t) && t < best)
        {
            best = t;
            hit = go;
        }
    }
    return hit;
}
