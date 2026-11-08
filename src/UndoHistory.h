#pragma once
#include <deque>
#include <vector>
#include <glad/glad.h>

#include "GameObject.h"

class Scene;

// Undo/Redo del editor por snapshots de la escena.
//
// Un snapshot es una copia de los GameObject. Las mallas son MeshPtr compartidos: el snapshot
// mantiene viva la malla de un objeto borrado, y deshacer solo vuelve a enlazarla (ni se copia ni
// se vuelve a subir a la GPU). Las texturas se comparten por handle, asi que la escena le pregunta al historial
// (UsesTexture) antes de liberar una: no se borra mientras algun snapshot pueda volver a necesitarla.
class UndoHistory
{
public:
    // Toma el estado actual como punto de partida: el primer cambio apilara este en undo.
    void Reset(const Scene& scene, int selected);

    // Llamar una vez por frame, con la UI ya construida. Aplica el undo/redo pedido y, si la escena
    // cambio, apila el estado anterior. Los cambios se agrupan: mientras haya un widget activo
    // (arrastrar un DragFloat, escribir un nombre) no se confirma nada, asi un arrastre entero es
    // un solo paso de undo.
    void Update(Scene& scene, int& selected);

    void RequestUndo() { wantUndo = true; }
    void RequestRedo() { wantRedo = true; }
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    // true si algun snapshot guardado usa la textura.
    bool UsesTexture(GLuint tex) const;

    // Suelta todos los snapshots (y con ellos sus mallas). Llamar antes de destruir el contexto de GL.
    void Clear();

private:
    struct SceneState {
        std::vector<GameObject> objects;
        int selected = -1;
    };

    static constexpr size_t UNDO_LIMIT = 30;

    std::deque<SceneState> undoStack, redoStack;
    SceneState committed;              // ultimo estado ya confirmado; se apila en undo al cambiar la escena
    bool wantUndo = false, wantRedo = false;

    static SceneState Capture(const Scene& scene, int selected);
    static bool Matches(const SceneState& s, const Scene& scene);
    void Apply(SceneState& s, Scene& scene, int& selected);
};
