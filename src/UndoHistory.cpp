#include "UndoHistory.h"

#include <algorithm>
#include <imgui.h>

#include "Scene.h"

void UndoHistory::Reset(const Scene& scene, int selected)
{
    committed = Capture(scene, selected);
}

UndoHistory::SceneState UndoHistory::Capture(const Scene& scene, int selected)
{
    SceneState s;
    s.objects = scene.objects;
    for (GameObject& o : s.objects) {
        Mesh& m = o.meshComp.mesh;
        s.meshIds.push_back(m.VAO);
        m.VAO = m.VBO = m.EBO = 0;
        m.faceNormalVAO = m.faceNormalVBO = 0;
        m.faceNormalCount = 0;
    }
    s.selected = selected;
    return s;
}

// Compara la escena viva con un estado guardado (la seleccion no cuenta).
bool UndoHistory::Matches(const SceneState& s, const Scene& scene)
{
    const std::vector<GameObject>& objects = scene.objects;
    if (s.objects.size() != objects.size()) return false;
    for (size_t i = 0; i < objects.size(); ++i) {
        const GameObject& a = s.objects[i];
        const GameObject& b = objects[i];
        if (s.meshIds[i] != b.meshComp.mesh.VAO || a.name != b.name || a.hidden != b.hidden ||
            a.normalsMode != b.normalsMode || a.showFaceNormals != b.showFaceNormals ||
            a.texComp.textureID != b.texComp.textureID || a.texComp.path != b.texComp.path ||
            a.transform.position != b.transform.position ||
            a.transform.rotation != b.transform.rotation ||
            a.transform.scale != b.transform.scale)
            return false;
    }
    return true;
}

// Sustituye la escena viva por el estado dado (que se consume).
void UndoHistory::Apply(SceneState& s, Scene& scene, int& selected)
{
    for (GameObject& o : scene.objects) o.Destroy();
    scene.objects = std::move(s.objects);
    for (GameObject& o : scene.objects) o.meshComp.mesh.SetupMesh();
    selected = (s.selected >= 0 && s.selected < (int)scene.objects.size()) ? s.selected : -1;
    scene.structureDirty = true;
    committed = Capture(scene, selected);   // refresca los VAO nuevos y evita registrar la restauracion como cambio
}

void UndoHistory::Update(Scene& scene, int& selected)
{
    const bool canEdit = !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput;

    if (canEdit && wantUndo && !undoStack.empty()) {
        SceneState target = std::move(undoStack.back());
        undoStack.pop_back();
        redoStack.push_back(std::move(committed));
        Apply(target, scene, selected);
    }
    else if (canEdit && wantRedo && !redoStack.empty()) {
        SceneState target = std::move(redoStack.back());
        redoStack.pop_back();
        undoStack.push_back(std::move(committed));
        Apply(target, scene, selected);
    }
    wantUndo = wantRedo = false;

    if (Matches(committed, scene)) {
        committed.selected = selected;   // la seleccion previa al cambio es la que se recupera al deshacer
        return;
    }
    if (!ImGui::IsAnyItemActive()) {
        std::vector<SceneState> discarded;
        undoStack.push_back(std::move(committed));
        while (undoStack.size() > UNDO_LIMIT) {
            discarded.push_back(std::move(undoStack.front()));
            undoStack.pop_front();
        }
        for (SceneState& s : redoStack) discarded.push_back(std::move(s));
        redoStack.clear();
        committed = Capture(scene, selected);
        // Los estados descartados ya no estan en el historial, asi que UsesTexture no los cuenta.
        for (const SceneState& s : discarded)
            for (const GameObject& o : s.objects)
                scene.ReleaseTextureIfUnused(o.texComp.textureID);
    }
}

bool UndoHistory::UsesTexture(GLuint tex) const
{
    auto usesTex = [tex](const GameObject& o) { return o.texComp.textureID == tex; };
    auto stateUsesTex = [&](const SceneState& s) { return std::any_of(s.objects.begin(), s.objects.end(), usesTex); };
    return stateUsesTex(committed)
        || std::any_of(undoStack.begin(), undoStack.end(), stateUsesTex)
        || std::any_of(redoStack.begin(), redoStack.end(), stateUsesTex);
}
