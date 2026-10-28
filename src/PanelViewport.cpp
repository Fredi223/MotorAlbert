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
    ImGui::TextColored(
        renderer.frustumCulling ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
        : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
        "Frustum Culling: %s",
        renderer.frustumCulling ? "ON" : "OFF"
    );

    ImGui::Text("Drawn objects: %d", drawnCount);
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
    // SELECCIÓN DE OBJETOS (RAYCAST CONTRA LOS AABB)
    // ========================================================
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !altHeld && mouseInsideViewport && !ImGui::IsAnyItemHovered())
    {
        const float PICK_TOLERANCE_PIXELS = 3.0f;   // margen de click alrededor del AABB, en pixeles de pantalla
        const float pickPixelToWorld = 2.0f * std::tan(glm::radians(Camera::FOV_DEG) * 0.5f) / viewportHeightF; // mundo por pixel y por unidad de profundidad

        // relMouseX/Y ya son relativos al viewport (origen arriba-izq), tal como los espera la funcion
        glm::vec3 rayDir = ScreenToWorldRay_UnProject(
            relMouseX, relMouseY,
            viewportWidthF, viewportHeightF,
            projection, view);

        glm::vec3 rayOrigin = camera.position;

        float closestDistance = std::numeric_limits<float>::max();
        int hitIndex = -1;

        // Candidatos: los objetos de las hojas del octree que toca la caja que envuelve el rayo.
        const glm::vec3 rayEnd = rayOrigin + rayDir * 10000.0f;
        std::vector<GameObject*> candidates;
        scene.octree.Query(glm::min(rayOrigin, rayEnd), glm::max(rayOrigin, rayEnd), candidates);

        for (GameObject* goPtr : candidates)
        {
            GameObject& go = *goPtr;
            int i = int(&go - &scene.objects[0]);
            if (go.hidden || go.meshComp.mesh.vertices.empty()) continue;

            float tHit = 0.0f;

            // Tolerancia de click: como maximo PICK_TOLERANCE_PIXELS pixeles alrededor del AABB. Un
            // pixel a profundidad d equivale a d * 2*tan(fov/2) / altoViewport unidades de mundo, asi el
            // margen es parecido en pantalla cerca o lejos (uno fijo en mundo no lo es). Se usa una cota
            // inferior de la profundidad de cualquier punto del AABB (la del centro menos la semidiagonal)
            // para que en sus caras mas cercanas, donde un pixel mide menos, el margen no se dispare.
            const glm::vec3 aabbCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
            const float halfDiag = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
            const float depth = std::max(glm::dot(aabbCenter - rayOrigin, camera.front) - halfDiag, 0.1f);
            const float pickInflate = PICK_TOLERANCE_PIXELS * depth * pickPixelToWorld;

            glm::vec3 pickMin = go.worldAabbMin - glm::vec3(pickInflate);
            glm::vec3 pickMax = go.worldAabbMax + glm::vec3(pickInflate);

            if (!RayIntersectsAABB(rayOrigin, rayDir, pickMin, pickMax, tHit))
                continue;

            // rayDir esta normalizado, asi que tHit ya es la distancia en unidades de mundo.
            if (tHit > 0.0f && tHit < closestDistance)
            {
                closestDistance = tHit;
                hitIndex = i;
            }
        }

        if (hitIndex != -1)
        {
            selectedObject = hitIndex;
            Console::LogInfo("Seleccionado: %s (index=%d)", scene.objects[selectedObject].name.c_str(), selectedObject);
        }
        else
        {
            selectedObject = -1;
            Console::LogInfo("Seleccion: ninguno");
        }
    }

    ImGui::Image((ImTextureID)(intptr_t)renderer.GetViewportTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End(); // cierra Viewport
}
