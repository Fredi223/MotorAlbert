#include "UndoHistory.h"

#include <algorithm>
#include <unordered_map>
#include <imgui.h>

#include "Scene.h"

void UndoHistory::Reset(const Scene& scene, const GameObject* selected)
{
    committed = Capture(scene, selected);
}

UndoHistory::SceneState UndoHistory::Capture(const Scene& scene, const GameObject* selected)
{
    SceneState s;
    scene.ForEach([&s](const GameObject& go) {
        s.objects.push_back({ go, go.parent ? go.parent->uid : 0 });
    });
    s.selectedUid = selected ? selected->uid : 0;
    return s;
}

// Sustituye la escena viva por el estado dado (que se consume).
void UndoHistory::Apply(SceneState& s, Scene& scene, GameObject*& selected)
{
    scene.Clear();

    // Preorden: el padre de cada objeto ya esta creado cuando llega el objeto.
    std::unordered_map<uint64_t, GameObject*> byUid{ { 0, &scene.root } };
    for (ObjectState& state : s.objects)
    {
        auto go = std::make_unique<GameObject>();
        static_cast<GameObjectData&>(*go) = std::move(state.data);
        GameObject* parent = byUid.at(state.parentUid);
        go->parent = parent;
        byUid[go->uid] = go.get();
        parent->children.push_back(std::move(go));
    }
    scene.UpdateTransforms();

    const auto sel = byUid.find(s.selectedUid);
    selected = (s.selectedUid != 0 && sel != byUid.end()) ? sel->second : nullptr;
    scene.structureDirty = true;
    committed = Capture(scene, selected);   // evita registrar la restauracion como un cambio
}

void UndoHistory::Update(Scene& scene, GameObject*& selected)
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

    SceneState current = Capture(scene, selected);
    if (current.objects == committed.objects) {
        committed.selectedUid = current.selectedUid;   // la seleccion previa al cambio es la que se recupera al deshacer
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
        committed = std::move(current);
        // Los estados descartados ya no estan en el historial, asi que UsesTexture no los cuenta.
        for (const SceneState& s : discarded)
            for (const ObjectState& o : s.objects)
                scene.ReleaseTextureIfUnused(o.data.texComp.textureID);
    }
}

bool UndoHistory::UsesTexture(GLuint tex) const
{
    auto usesTex = [tex](const ObjectState& o) { return o.data.texComp.textureID == tex; };
    auto stateUsesTex = [&](const SceneState& s) { return std::any_of(s.objects.begin(), s.objects.end(), usesTex); };
    return stateUsesTex(committed)
        || std::any_of(undoStack.begin(), undoStack.end(), stateUsesTex)
        || std::any_of(redoStack.begin(), redoStack.end(), stateUsesTex);
}

void UndoHistory::Clear()
{
    undoStack.clear();
    redoStack.clear();
    committed = SceneState{};
}
