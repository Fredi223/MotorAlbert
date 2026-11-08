#pragma once
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

struct Frustum;
struct GameObject;

// Octree de los GameObject con malla, indexados por su AABB de mundo. Solo las hojas guardan
// objetos; uno que cruza el limite entre hojas esta en todas las que toca (las consultas quitan
// los repetidos). Guarda punteros: hay que reconstruirlo (Scene::RebuildOctree) cuando los
// objetos se borran o se mueven.
//
// Las consultas descartan un nodo entero (con todo su subarbol) si su caja no toca el volumen
// consultado: eso es lo que ahorra el octree frente a recorrer todos los objetos. Devuelven
// CANDIDATOS: el test fino (AABB del objeto contra el frustum, rayo contra triangulos) lo hace
// quien consulta.
class Octree
{
public:
    struct Node
    {
        glm::vec3 min{ 0.0f };
        glm::vec3 max{ 0.0f };
        std::vector<GameObject*> objects;
        std::array<std::unique_ptr<Node>, 8> children;

        bool IsLeaf() const { return children[0] == nullptr; }
    };

    // Cuantos nodos ha mirado la ultima consulta y cuantos tiene el arbol (Configuration los muestra).
    struct QueryStats { int nodesVisited = 0; int nodesTotal = 0; };

    // Vacia el arbol y lo deja con una sola hoja que cubre [min, max].
    void Rebuild(const glm::vec3& min, const glm::vec3& max);
    void Insert(GameObject* go);

    // Objetos de las hojas que tocan el frustum.
    void QueryFrustum(const Frustum& frustum, std::vector<GameObject*>& results, QueryStats* stats = nullptr) const;
    // Objetos de las hojas que atraviesa el rayo (rayDir normalizado).
    void QueryRay(const glm::vec3& rayOrig, const glm::vec3& rayDir, std::vector<GameObject*>& results) const;
    // Todos los objetos (sin repetir).
    void QueryAll(std::vector<GameObject*>& results) const;

    // Recorre todos los nodos (para dibujar el wireframe de debug).
    void ForEachNode(const std::function<void(const Node&)>& f) const;

    int NodeCount() const { return nodeCount; }

private:
    static constexpr size_t MAX_OBJECTS = 4;   // objetos por hoja antes de subdividirla...
    static constexpr int MAX_DEPTH = 6;        // ...salvo a esta profundidad

    std::unique_ptr<Node> root;
    int nodeCount = 0;

    void Insert(Node& node, GameObject* go, int depth);
    void Subdivide(Node& node);
    // Recorre los nodos cuya caja acepta nodeTest y anade los objetos de las hojas a results.
    template <class Test> void Collect(const Node& node, const Test& nodeTest,
        std::vector<GameObject*>& results, int& visited) const;
    static void RemoveDuplicates(std::vector<GameObject*>& results, size_t firstNew);
};
