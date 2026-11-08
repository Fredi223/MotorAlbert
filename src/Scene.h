#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glad/glad.h>

#include "GameObject.h"
#include "ModuleModelLoader.h"
#include "Octree.h"

class ResourceManager;

// Escena del motor: el arbol de GameObjects y el octree que indexa los que tienen malla.
//
// La raiz (root) es un GameObject invisible que no se muestra ni se edita: sus hijos son los objetos
// de primer nivel. Cada objeto es dueno de sus hijos, asi que las direcciones son estables: anadir
// objetos no invalida punteros. BORRAR si (el octree guarda GameObject*), por eso toda mutacion
// estructural (borrar, reparentar, cargar...) marca structureDirty y el bucle principal llama a
// RebuildOctree antes de volver a consultar el octree.
class Scene
{
public:
    Scene();

    GameObject root;
    Octree octree;
    bool structureDirty = false;

    // Recorre todos los objetos (sin la raiz) en preorden: cada padre antes que sus hijos y los
    // hermanos en orden. f no debe anadir ni quitar objetos.
    template <class F> void ForEach(F&& f) { Visit<GameObject>(root, f); }
    template <class F> void ForEach(F&& f) const { Visit<const GameObject>(root, f); }

    GameObject* FindByUid(uint64_t uid);

    // La camara de juego que hace el frustum culling: la primera con ComponentCamera activo, marcada como
    // cullingCamera y no oculta. nullptr = se usa la camara del editor.
    const GameObject* FindCullingCamera() const;

    // Crea una camara de juego (GameObject con ComponentCamera) de primer nivel.
    GameObject* CreateCamera(const glm::mat4& worldTransform);

    // Recalcula la matriz de mundo y el AABB de mundo de todos los objetos.
    void UpdateTransforms();

    // Recalcula los limites del octree a partir de los objetos y vuelve a insertar los que tienen malla.
    void RebuildOctree();

    // Crea los GameObjects de un modelo importado (su .model de Library), pidiendo cada malla y textura
    // al gestor de recursos. Devuelve la raiz del modelo (de primer nivel), o nullptr si falla.
    GameObject* InstantiateModel(uint64_t modelUid, ResourceManager& resources);

    // Crea un GameObject sin malla, hijo de parent (nullptr = primer nivel).
    GameObject* CreateEmpty(const std::string& name, GameObject* parent = nullptr);

    // Crea un GameObject de primer nivel con una malla (una primitiva) y un material sin textura.
    GameObject* AddPrimitive(const char* name, MeshPtr mesh);

    // Copia el objeto con toda su rama justo detras del original (comparten mallas y texturas).
    // Devuelve la copia.
    GameObject* Duplicate(GameObject* go);

    // Borra el objeto y toda su rama. Quien guarde punteros a ellos (la seleccion) debe soltarlos antes.
    void Erase(GameObject* go);

    // Cambia el padre de go (nullptr = primer nivel) conservando su transform de MUNDO. Falla (false)
    // si el nuevo padre es el propio objeto o uno de sus descendientes, o si ya es su padre.
    bool Reparent(GameObject* go, GameObject* newParent);

    // Borra todos los objetos.
    void Clear();

private:
    template <class Node, class F> static void Visit(Node& node, F& f)
    {
        for (const auto& child : node.children)
        {
            Node& c = *child;
            f(c);
            Visit<Node>(c, f);
        }
    }

    GameObject* AddChild(GameObject* parent, std::unique_ptr<GameObject> child);
};
