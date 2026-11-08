#include "Editor.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scene.h"

void Editor::DrawInspector()
{
    if (showInspector)
    {
        ImGui::Begin("Inspector");

        if (selectedObject)
        {
            GameObject& go = *selectedObject;
            char nameBuf[128];
            std::strncpy(nameBuf, go.name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
            {
                go.name = std::string(nameBuf);
            }
            ImGui::TextDisabled("Parent: %s | Children: %zu",
                go.parent && go.parent != &scene.root ? go.parent->name.c_str() : "<scene>", go.children.size());

            ImGui::Separator();

            // ---------------- Transform ----------------
            if (ImGui::CollapsingHeader("Transform (local)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // Se edita el transform LOCAL (relativo al padre). Las matrices y AABB de mundo de
                // el y de sus hijos se recalculan antes de dibujar (Scene::UpdateTransforms).
                bool transformCommitted = false;  // el gesto de edicion (arrastre o tecleo) ha terminado

                ImGui::DragFloat3("Position", glm::value_ptr(go.transform.position), 0.1f);
                transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DragFloat3("Rotation", glm::value_ptr(go.transform.rotation), 0.5f);
                transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::DragFloat3("Scale", glm::value_ptr(go.transform.scale), 0.05f))
                {
                    // Escala 0 haria la matriz no invertible (el reparent la invierte). Se conserva
                    // el signo: una escala negativa (espejo) es valida.
                    for (int i = 0; i < 3; ++i)
                        if (std::abs(go.transform.scale[i]) < 0.001f)
                            go.transform.scale[i] = std::copysign(0.001f, go.transform.scale[i]);
                }
                transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::Button("Reset Transform")) {
                    go.transform = Transform{};
                    transformCommitted = true;
                }

                // El octree solo sabe reconstruirse entero (y cada reconstruccion escribe en la
                // consola): se reconstruye una vez al terminar el gesto, no en cada frame de un
                // arrastre. Mientras se arrastra, la consulta de dibujo (caja de +-1000) sigue
                // devolviendo el objeto aunque su hoja del octree sea la antigua.
                if (transformCommitted)
                {
                    scene.structureDirty = true;
                }
            }

            if (!go.HasMesh())
            {
                ImGui::Separator();
                ImGui::TextDisabled("Sin malla (GameObject vacio)");
            }
            else
            {
                ImGui::Separator();

                // ---------------- Mesh ----------------
                if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const Mesh& mesh = *go.meshComp.mesh;
                    ImGui::Text("Vertices: %u", mesh.vertexCount);
                    ImGui::Text("Indices: %u", mesh.indexCount);
                    ImGui::Text("VAO: %u", mesh.VAO);
                    ImGui::Text("TexturePath: %s", mesh.texturePath.empty() ? "<none>" : mesh.texturePath.c_str());

                    static const char* normalsModes[] = { "Use Global", "Force On", "Force Off" };
                    int mode = static_cast<int>(go.normalsMode);
                    if (ImGui::Combo("Normals Mode", &mode, normalsModes, IM_ARRAYSIZE(normalsModes)))
                    {
                        go.normalsMode = static_cast<GameObject::NormalsMode>(mode);
                    }
                    bool forceOnNormals = (go.normalsMode == GameObject::NM_ForceOn);
                    if (ImGui::Checkbox("Mostrar normales (mesh)", &forceOnNormals))
                    {
                        go.normalsMode = forceOnNormals ? GameObject::NM_ForceOn : GameObject::NM_UseGlobal;
                        Console::LogInfo("Mostrar normales (local) de %s: %s", go.name.c_str(), normalsModes[go.normalsMode]);
                    }

                    ImGui::Checkbox("Mostrar normales por cara (mesh)", &go.showFaceNormals);
                    ImGui::TextDisabled("Global Normals: %s | Face Normals: %s", renderer.showNormals ? "ON" : "OFF", renderer.showFaceNormals ? "ON" : "OFF");
                }

                ImGui::Separator();

                // ---------------- Texture ----------------
                if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("TextureID: %u", go.texComp.textureID);
                    ImGui::TextWrapped("Path: %s", go.texComp.path.empty() ? "<none>" : go.texComp.path.c_str());

                    if (go.texComp.textureID != 0)
                    {
                        // El tamano se pregunta a OpenGL (nivel 0 de la textura) en vez de guardarlo,
                        // asi vale para cualquier origen: FBX, drag & drop o checker.
                        GLint texW = 0, texH = 0;
                        glGetTextureLevelParameteriv(go.texComp.textureID, 0, GL_TEXTURE_WIDTH, &texW);
                        glGetTextureLevelParameteriv(go.texComp.textureID, 0, GL_TEXTURE_HEIGHT, &texH);
                        ImGui::Text("Size: %d x %d", texW, texH);

                        ImGui::Image((ImTextureID)(intptr_t)go.texComp.textureID, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
                        if (ImGui::Button("Remove Texture"))
                        {
                            const GLuint oldTex = go.texComp.textureID;
                            go.texComp.textureID = 0;
                            go.texComp.path.clear();
                            scene.ReleaseTextureIfUnused(oldTex);
                            Console::LogInfo("Texture removed from %s", go.name.c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Open Folder")) {
                            if (!go.texComp.path.empty()) {
                                // Sin shell de por medio: una ruta con comillas o '&' no puede
                                // inyectar comandos, como pasaba al montar la linea de explorer.
                                const std::string dir = std::filesystem::path(go.texComp.path).parent_path().string();
                                SDL_OpenURL(dir.c_str());
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("No texture assigned");
                    }

                    if (ImGui::Button("Assign CheckerTexture"))
                    {
                        const GLuint oldTex = go.texComp.textureID;
                        go.texComp.textureID = renderer.GetCheckerTexture();
                        go.texComp.path.clear();
                        scene.ReleaseTextureIfUnused(oldTex);
                        Console::LogInfo("Checker texture asignada a %s", go.name.c_str());
                    }
                }
            }
        }
        else
        {
            ImGui::Text("No hay ningun objeto seleccionado");
        }

        ImGui::End();
    }
}
