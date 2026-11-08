#include "Editor.h"

#include <algorithm>
#include <vector>
#include <assimp/version.h>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "FrameTiming.h"
#include "GameObject.h"
#include "Logger.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SystemInfo.h"

void Editor::DrawConfiguration()
{
    if (showConfig) {
        ImGui::Begin("Configuration", &showConfig);

        if (ImGui::CollapsingHeader("System / Graphics Info", ImGuiTreeNodeFlags_DefaultOpen)) {

            ImGui::Text("SDL Version: %d.%d.%d",
                SDL_MAJOR_VERSION,
                SDL_MINOR_VERSION,
                SDL_MICRO_VERSION);

            ImGui::Text("CPU Cores: %d", SDL_GetNumLogicalCPUCores());
            ImGui::Text("CPU Cacheline: %d bytes", SDL_GetCPUCacheLineSize());
            ImGui::Text("System RAM: %d MB", SDL_GetSystemRAM());

            // OpenGL / GPU
            const GLubyte* glVendor = glGetString(GL_VENDOR);
            const GLubyte* glRenderer = glGetString(GL_RENDERER);
            const GLubyte* glVersion = glGetString(GL_VERSION);
            const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
            ImGui::Separator();
            ImGui::Text("GPU Vendor: %s", glVendor ? (const char*)glVendor : "unknown");
            ImGui::Text("GPU Renderer: %s", glRenderer ? (const char*)glRenderer : "unknown");
            ImGui::Text("OpenGL Version: %s", glVersion ? (const char*)glVersion : "unknown");
            ImGui::Text("GLSL Version: %s", glslVersion ? (const char*)glslVersion : "unknown");

            ImGui::Text("Assimp: %u.%u.%u", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionRevision());
        }

        // --- Viewport / Window config ---
        if (ImGui::CollapsingHeader("Window / Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputInt("Width", &winW);
            ImGui::InputInt("Height", &winH);
            if (ImGui::Button("Apply Window Size")) {
                SDL_SetWindowSize(window, winW, winH);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset Window Size")) {
                SDL_SetWindowSize(window, 1280, 720);
            }

            ImGui::Checkbox("VSync", &timing.vsync);
            if (ImGui::IsItemEdited()) {
                timing.ApplyVsync();
            }

            ImGui::InputInt("FPS Cap (0 = uncapped)", &timing.fpsCap);
            if (timing.fpsCap < 0) timing.fpsCap = 0;

            ImGui::ColorEdit3("Viewport Clear Color", glm::value_ptr(renderer.clearColor));

            ImGui::Separator();
            ImGui::Text("Culling");

            ImGui::Checkbox("Enable Frustum Culling", &renderer.frustumCulling);
            const RenderStats& rs = renderStats;
            ImGui::Text("Culling camera: %s", rs.cullingCamera.empty() ? "editor camera" : rs.cullingCamera.c_str());
            ImGui::Text("Objects drawn: %d   culled: %d", rs.drawn, rs.culled);
            ImGui::Text("Octree: %d candidates, %d of %d nodes visited", rs.candidates, rs.nodesVisited, rs.nodesTotal);
            ImGui::TextDisabled("Debug draw (AABB, octree, rayo, frustums): menu View > Debug Draw");
        }

        // --- Input / camara ---
        if (ImGui::CollapsingHeader("Input / Camera")) {
            ImGui::DragFloat("Move speed (units/s)", &camera.moveSpeed, 0.1f, 0.1f, 100.0f);
            ImGui::DragFloat("Mouse sensitivity", &camera.mouseSensitivity, 0.005f, 0.01f, 1.0f);
            ImGui::DragFloat("Zoom step (wheel)", &camera.zoomStep, 0.005f, 0.01f, 0.5f);
            ImGui::Text("Sprint (Shift): x%.1f", Camera::SPRINT_MULTIPLIER);
            ImGui::Separator();
            ImGui::Text("Editor camera lens");
            DrawLensFields(camera.lens);
            ImGui::Text("Camera: (%.2f, %.2f, %.2f)  yaw %.1f  pitch %.1f",
                camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch);
        }

        // --- Texturas ---
        if (ImGui::CollapsingHeader("Textures")) {
            const char* filters[] = { "Linear + mipmaps", "Nearest" };
            const char* wraps[] = { "Repeat", "Clamp to edge", "Mirrored repeat" };
            const bool filterChanged = ImGui::Combo("Filter", &texFilter, filters, IM_ARRAYSIZE(filters));
            const bool wrapChanged = ImGui::Combo("Wrap", &texWrap, wraps, IM_ARRAYSIZE(wraps));
            ImGui::TextDisabled("Se aplica a las texturas ya cargadas");

            // Las texturas cargadas (con alguna referencia) y la checker.
            std::vector<GLuint> texIds{ renderer.GetCheckerTexture() };
            for (const auto& [uid, entry] : resources.Entries())
                if (const TexturePtr tex = entry.texture.lock()) texIds.push_back(tex->id);

            if (filterChanged || wrapChanged) {
                const GLint minF = texFilter == 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST;
                const GLint magF = texFilter == 0 ? GL_LINEAR : GL_NEAREST;
                const GLint wrap = texWrap == 0 ? GL_REPEAT : (texWrap == 1 ? GL_CLAMP_TO_EDGE : GL_MIRRORED_REPEAT);
                for (GLuint t : texIds) {
                    glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, minF);
                    glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, magF);
                    glTextureParameteri(t, GL_TEXTURE_WRAP_S, wrap);
                    glTextureParameteri(t, GL_TEXTURE_WRAP_T, wrap);
                }
                Console::LogInfo("Texturas: filtro=%s, wrap=%s (%zu texturas)", filters[texFilter], wraps[texWrap], texIds.size());
            }

            // Estimacion de VRAM: RGBA8 (4 B/pixel) + ~1/3 extra de la cadena de mipmaps.
            double texBytes = 0.0;
            for (GLuint t : texIds) {
                GLint w = 0, h = 0;
                glGetTextureLevelParameteriv(t, 0, GL_TEXTURE_WIDTH, &w);
                glGetTextureLevelParameteriv(t, 0, GL_TEXTURE_HEIGHT, &h);
                texBytes += double(w) * h * 4.0 * (4.0 / 3.0);
            }
            ImGui::Text("Loaded textures: %zu (~%.2f MB)", texIds.size(), texBytes / (1024.0 * 1024.0));
        }

        // --- Memoria ---
        if (ImGui::CollapsingHeader("Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
            size_t workingSet = 0, privateBytes = 0;
            if (SystemInfo::GetProcessMemory(workingSet, privateBytes)) {
                ImGui::Text("Process RAM (working set): %.1f MB", workingSet / (1024.0 * 1024.0));
                ImGui::Text("Process committed (private): %.1f MB", privateBytes / (1024.0 * 1024.0));
            }
            else {
                ImGui::TextDisabled("Process memory: not available on this platform");
            }

            // Las mallas compartidas (duplicados) se cuentan una vez: estan una sola vez en memoria.
            size_t objectCount = 0;
            std::vector<const Mesh*> meshes;
            scene.ForEach([&](const GameObject& o) {
                ++objectCount;
                if (const auto* m = o.components.Get<ComponentMesh>(); m && m->mesh) meshes.push_back(m->mesh.get());
            });
            std::sort(meshes.begin(), meshes.end());
            meshes.erase(std::unique(meshes.begin(), meshes.end()), meshes.end());

            size_t vertexCount = 0, indexCount = 0, meshBytes = 0;
            for (const Mesh* m : meshes) {
                vertexCount += m->vertexCount;
                indexCount += m->indexCount;
                meshBytes += m->vertices.size() * sizeof(float) + m->indices.size() * sizeof(unsigned int);
            }
            ImGui::Text("Scene: %zu objects, %zu meshes, %zu vertices, %zu indices", objectCount, meshes.size(), vertexCount, indexCount);
            ImGui::Text("Mesh data: %.2f MB (CPU, same size on GPU)", meshBytes / (1024.0 * 1024.0));
        }

        // --- Performance / graphs ---
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            float avg = 0.0f;
            for (float t : timing.FrameTimesMs()) avg += t;
            avg = timing.FrameTimesMs().empty() ? 0.0f : avg / (float)timing.FrameTimesMs().size();
            float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;

            ImGui::Text("FPS (avg): %.1f", fps);
            ImGui::Text("Frame ms (avg): %.3f", avg);
            // PlotLines necesita memoria contigua y un deque no lo es: se copia a un vector.
            plotBuf.assign(timing.FrameTimesMs().begin(), timing.FrameTimesMs().end());

            ImGui::PlotLines("Frame ms", plotBuf.data(), (int)plotBuf.size(), 0, nullptr, 0.0f, 50.0f, ImVec2(0, 80));
            ImGui::Text("Last frame time: %.3f ms", plotBuf.empty() ? 0.0f : plotBuf.back());
        }

        ImGui::End();
    }
}
