#include "Octree.h"
#include "GameObject.h"
#include <algorithm>
#include <functional>

// ------------------------------------------------------------

Octree::Octree(const glm::vec3& min, const glm::vec3& max)
{
    root = new OctreeNode();
    root->min = min;
    root->max = max;
}

Octree::~Octree()
{
    Clear();
}

// ------------------------------------------------------------

void Octree::Clear()
{
    std::function<void(OctreeNode*)> deleteNode = [&](OctreeNode* node)
        {
            if (!node) return;
            for (int i = 0; i < 8; ++i)
                deleteNode(node->children[i]);
            delete node;
        };

    deleteNode(root);
    root = nullptr;
}

// ------------------------------------------------------------

void Octree::Rebuild(const glm::vec3& min, const glm::vec3& max)
{
    Clear();

    root = new OctreeNode();
    root->min = min;
    root->max = max;
}

// ------------------------------------------------------------

void Octree::Insert(GameObject* go)
{
    Insert(root, go, 0);
}

void Octree::Insert(OctreeNode* node, GameObject* go, int depth)
{
    if (!node) return;

    if (node->IsLeaf() && (node->objects.size() < maxObjects || depth >= maxDepth))
    {
        node->objects.push_back(go);
        return;
    }

    if (node->IsLeaf())
        Subdivide(node);

    for (int i = 0; i < 8; ++i)
    {
        if (Intersects(
            go->worldAabbMin, go->worldAabbMax,
            node->children[i]->min, node->children[i]->max))
        {
            Insert(node->children[i], go, depth + 1);
        }
    }
}

// ------------------------------------------------------------

void Octree::Subdivide(OctreeNode* node)
{
    glm::vec3 size = (node->max - node->min) * 0.5f;

    for (int i = 0; i < 8; ++i)
    {
        node->children[i] = new OctreeNode();

        glm::vec3 offset(
            (i & 1) ? size.x : 0.0f,
            (i & 2) ? size.y : 0.0f,
            (i & 4) ? size.z : 0.0f
        );

        node->children[i]->min = node->min + offset;
        node->children[i]->max = node->children[i]->min + size;
    }
}

// ------------------------------------------------------------

void Octree::Query(const glm::vec3& qMin, const glm::vec3& qMax,
    std::vector<GameObject*>& results) const
{
    std::function<void(const OctreeNode*)> queryNode =
        [&](const OctreeNode* node)
        {
            if (!node) return;

            if (!Intersects(node->min, node->max, qMin, qMax))
                return;

            for (GameObject* go : node->objects)
                results.push_back(go);

            if (!node->IsLeaf())
            {
                for (int i = 0; i < 8; ++i)
                    queryNode(node->children[i]);
            }
        };

    const size_t firstNew = results.size();
    queryNode(root);

    // Un objeto que cruza el limite entre varios nodos esta insertado en cada uno de ellos
    // (ver Insert), asi que la consulta lo devuelve repetido. Se eliminan los duplicados
    // de lo anadido por esta consulta, sin tocar lo que el llamador ya tuviera en results.
    std::sort(results.begin() + firstNew, results.end());
    results.erase(std::unique(results.begin() + firstNew, results.end()), results.end());
}

// ------------------------------------------------------------

bool Octree::Intersects(const glm::vec3& aMin, const glm::vec3& aMax,
    const glm::vec3& bMin, const glm::vec3& bMax) const
{
    return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
        (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
        (aMin.z <= bMax.z && aMax.z >= bMin.z);
}
