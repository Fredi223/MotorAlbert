#include "Editor.h"

#include <algorithm>

#include "imgui.h"
#include "FrameTiming.h"
#include "GameObject.h"
#include "Logger.h"
#include "Scene.h"

// Target orbital = centro del AABB de mundo del objeto seleccionado; sin selección, el origen.
static void GetOrbitTarget(std::vector<GameObject>& objs, int sel, glm::vec3& outCenter, float& outRadius)
{
    if (sel >= 0 && sel < (int)objs.size())
    {
        GameObject& go = objs[sel];
        go.UpdateWorldAABB(); // defensivo: garantiza un AABB fresco aunque otra ruta haya tocado el transform
        outCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
        outRadius = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
    }
    else
    {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.0f;
    }
}

// Captura del ratón compartida por cámara libre y orbital (son excluyentes).
void Editor::BeginMouseCapture()
{
    float mx = 0.f, my = 0.f;
    SDL_GetMouseState(&mx, &my);
    prevMouseX = (int)mx;
    prevMouseY = (int)my;

    SDL_SetWindowRelativeMouseMode(window, true);
    SDL_HideCursor();
    firstMouse = true;
}

void Editor::EndMouseCapture()
{
    SDL_SetWindowRelativeMouseMode(window, false);
    SDL_ShowCursor();
    SDL_WarpMouseInWindow(window, (float)prevMouseX, (float)prevMouseY);
}

// Delta de ratón desde el último frame (mismo en ambos modos).
void Editor::ReadMouseDelta(float& dx, float& dy)
{
    SDL_GetRelativeMouseState(&dx, &dy);
    // SDL acumula el movimiento desde la última llamada, y aquí solo se
    // llama con un botón mantenido: el primer delta trae todo el recorrido
    // del cursor hasta el viewport. Se descarta.
    if (firstMouse)
    {
        dx = dy = 0.f;
        firstMouse = false;
    }
}

// F: encuadra el objeto seleccionado manteniendo la dirección de mirada actual.
void Editor::FrameSelectedObject()
{
    if (selectedObject < 0 || selectedObject >= (int)scene.objects.size())
    {
        Console::LogInfo("F: no hay ningun objeto seleccionado que encuadrar");
        return;
    }
    GetOrbitTarget(scene.objects, selectedObject, camera.orbitTarget, camera.orbitRadius);
    camera.FrameTarget(viewportHeightF > 0.0f ? viewportWidthF / viewportHeightF : 1.0f);

    Console::LogInfo("Encuadrado %s: target=(%.2f, %.2f, %.2f) distancia=%.2f",
        scene.objects[selectedObject].name.c_str(),
        camera.orbitTarget.x, camera.orbitTarget.y, camera.orbitTarget.z, camera.orbitDistance);
}

// Calcula también view y projection del frame: es el único sitio donde se actualizan.
void Editor::UpdateCamera()
{
    ImGuiIO& io = ImGui::GetIO();

    // ========================================================
    //  CONTROL DE CÁMARA: LIBRE (click derecho) Y ORBITAL (Alt + click izquierdo)
    // ========================================================
    bool rightClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool rightClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    bool rightClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    bool leftClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool leftClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool leftClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    altHeld = io.KeyAlt;

    // Entrada en cada modo. Ambos capturan el ratón, así que son excluyentes.
    // mouseInsideViewport lo calcula el panel "Viewport" más abajo en el
    // frame, así que aquí vale el del frame anterior (mismo dato que usa el picking).
    if (mouseInsideViewport && !freeLookActive && !orbitActive)
    {
        if (rightClickPressed)
        {
            BeginMouseCapture();
            freeLookActive = true;
        }
        else if (leftClickPressed && altHeld)
        {
            BeginMouseCapture();
            orbitActive = true;

            // Target: objeto seleccionado (o el origen). distance: la actual cámara-target.
            GetOrbitTarget(scene.objects, selectedObject, camera.orbitTarget, camera.orbitRadius);
            camera.BeginOrbit();
        }
    }

    if (rightClickReleased && freeLookActive)
    {
        EndMouseCapture();
        freeLookActive = false;
    }

    if (leftClickReleased && orbitActive)
    {
        EndMouseCapture();
        orbitActive = false;
    }

    if (freeLookActive && rightClickHeld)
    {
        io.WantCaptureMouse = false;
        io.WantCaptureKeyboard = false;

        float dx, dy;
        ReadMouseDelta(dx, dy);
        camera.Rotate(dx, dy);

        // Duración del último frame completo (lo mide FrameTiming).
        float deltaTime = timing.LastFrameSeconds();

        const bool* keys = SDL_GetKeyboardState(NULL);
        float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
        float right = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
        float upAxis = (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
        bool sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        camera.Move(forward, right, upAxis, deltaTime, sprint);
    }

    if (orbitActive && leftClickHeld)
    {
        float dx, dy;
        ReadMouseDelta(dx, dy);
        camera.Orbit(dx, dy);
    }

    // Rueda: fuera del modo orbital, el target de referencia es el objeto seleccionado.
    if (wheelDelta != 0.0f)
    {
        if (!orbitActive)
            GetOrbitTarget(scene.objects, selectedObject, camera.orbitTarget, camera.orbitRadius);
        camera.Zoom(wheelDelta, orbitActive);
    }
    wheelDelta = 0.0f;

    // Único punto donde se recalcula view: refleja la cámara tras cualquier modo, la rueda
    // o F. El picking usa camera.position como origen del rayo, y debe coincidir.
    view = camera.GetViewMatrix();

    // ============================================================
    //  Actualizar proyección según el tamaño del viewport
    // ============================================================
    projection = camera.GetProjectionMatrix(viewportWidthF / viewportHeightF);
}
