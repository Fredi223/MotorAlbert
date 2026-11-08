#pragma once
#include <cstdint>
#include <deque>
#include <vector>
#include <glad/glad.h>

#include "GameObject.h"

class Scene;

// Undo/Redo del editor por snapshots de la escena.
//
// Un snapshot es la lista de los GameObject en preorden (cada padre antes que sus hijos), con sus
// datos (GameObjectData) y el UID de su padre: con eso se reconstruye el arbol tal cual. Las mallas
// son MeshPtr compartidos: el snapshot mantiene viva la malla de un objeto borrado, y deshacer solo
// vuelve a enlazarla (ni se copia ni se vuelve a subir a la GPU). Las texturas se comparten por
// handle, asi que la escena le pregunta al historial (UsesTexture) antes de liberar una: no se
// borra mientras algun snapshot pueda volver a necesitarla. La seleccion se guarda por UID.
class UndoHistory
{
public:
    // Toma el estado actual como punto de partida: el primer cambio apilara este en undo.
    void Reset(const Scene& scene, const GameObject* selected);

    // Llamar una vez por frame, con la UI ya construida. Aplica el undo/redo pedido y, si la escena
    // cambio, apila el estado anterior. Los cambios se agrupan: mientras haya un widget activo
    // (arrastrar un DragFloat, escribir un nombre) no se confirma nada, asi un arrastre entero es
    // un solo paso de undo. Al deshacer, selected pasa a apuntar al objeto restaurado (o nullptr).
    void Update(Scene& scene, GameObject*& selected);

    void RequestUndo() { wantUndo = true; }
    void RequestRedo() { wantRedo = true; }
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    // true si algun snapshot guardado usa la textura.
    bool UsesTexture(GLuint tex) const;

    // Suelta todos los snapshots (y con ellos sus mallas). Llamar antes de destruir el contexto de GL.
    void Clear();

private:
    struct ObjectState {
        GameObjectData data;
        uint64_t parentUid = 0;   // 0 = primer nivel

        bool operator==(const ObjectState&) const = default;
    };
    struct SceneState {
        std::vector<ObjectState> objects;   // preorden
        uint64_t selectedUid = 0;
    };

    static constexpr size_t UNDO_LIMIT = 30;

    std::deque<SceneState> undoStack, redoStack;
    SceneState committed;              // ultimo estado ya confirmado; se apila en undo al cambiar la escena
    bool wantUndo = false, wantRedo = false;

    static GLuint TextureOf(const ObjectState& o);
    static SceneState Capture(const Scene& scene, const GameObject* selected);
    void Apply(SceneState& s, Scene& scene, GameObject*& selected);
};
