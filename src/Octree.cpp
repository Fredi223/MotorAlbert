#include "Octree.h"

#include <algorithm>

#include "GameObject.h"
#include "Geometry.h"

static bool BoxesOverlap(const glm::vec3& aMin, const glm::vec3& aMax, const glm::vec3& bMin, const glm::vec3& bMax)
{
    return aMin.x <= bMax.x && aMax.x >= bMin.x &&
        aMin.y <= bMax.y && aMax.y >= bMin.y &&
        aMin.z <= bMax.z && aMax.z >= bMin.z;
}

void Octree::Rebuild(const glm::vec3& min, const glm::vec3& max)
{
    root = std::make_unique<Node>();
    root->min = min;
    root->max = max;
    nodeCount = 1;
}

void Octree::Insert(GameObject* go)
{
    if (root) Insert(*root, go, 0);
}

void Octree::Insert(Node& node, GameObject* go, int depth)
{
    if (node.IsLeaf())
    {
        if (node.objects.size() < MAX_OBJECTS || depth >= MAX_DEPTH)
        {
            node.objects.push_back(go);
            return;
        }
        // Hoja llena: se divide y sus objetos bajan a los hijos junto con el nuevo.
        Subdivide(node);
        std::vector<GameObject*> previous = std::move(node.objects);
        node.objects.clear();
        for (GameObject* old : previous) Insert(node, old, depth);
    }

    for (const auto& child : node.children)
        if (BoxesOverlap(go->worldAabbMin, go->worldAabbMax, child->min, child->max))
            Insert(*child, go, depth + 1);
}

void Octree::Subdivide(Node& node)
{
    const glm::vec3 half = (node.max - node.min) * 0.5f;
    for (int i = 0; i < 8; ++i)
    {
        auto child = std::make_unique<Node>();
        const glm::vec3 offset((i & 1) ? half.x : 0.0f, (i & 2) ? half.y : 0.0f, (i & 4) ? half.z : 0.0f);
        child->min = node.min + offset;
        child->max = child->min + half;
        node.children[i] = std::move(child);
    }
    nodeCount += 8;
}

template <class Test>
void Octree::Collect(const Node& node, const Test& nodeTest, std::vector<GameObject*>& results, int& visited) const
{
    ++visited;
    if (!nodeTest(node)) return;   // ni este nodo ni ninguno de sus hijos
    if (node.IsLeaf())
    {
        results.insert(results.end(), node.objects.begin(), node.objects.end());
        return;
    }
    for (const auto& child : node.children)
        Collect(*child, nodeTest, results, visited);
}

// Un objeto que cruza el limite entre hojas sale una vez por hoja. Se quitan los repetidos de lo
// anadido por esta consulta, sin tocar lo que el llamador ya tuviera en results.
void Octree::RemoveDuplicates(std::vector<GameObject*>& results, size_t firstNew)
{
    std::sort(results.begin() + firstNew, results.end());
    results.erase(std::unique(results.begin() + firstNew, results.end()), results.end());
}

void Octree::QueryFrustum(const Frustum& frustum, std::vector<GameObject*>& results, QueryStats* stats) const
{
    int visited = 0;
    const size_t firstNew = results.size();
    if (root)
        Collect(*root, [&frustum](const Node& n) { return AABBInsideFrustum(frustum, n.min, n.max); }, results, visited);
    RemoveDuplicates(results, firstNew);
    if (stats) *stats = { visited, nodeCount };
}

void Octree::QueryRay(const glm::vec3& rayOrig, const glm::vec3& rayDir, std::vector<GameObject*>& results) const
{
    int visited = 0;
    const size_t firstNew = results.size();
    if (root)
        Collect(*root, [&](const Node& n) { float t; return RayIntersectsAABB(rayOrig, rayDir, n.min, n.max, t); }, results, visited);
    RemoveDuplicates(results, firstNew);
}

void Octree::QueryAll(std::vector<GameObject*>& results) const
{
    int visited = 0;
    const size_t firstNew = results.size();
    if (root)
        Collect(*root, [](const Node&) { return true; }, results, visited);
    RemoveDuplicates(results, firstNew);
}

void Octree::ForEachNode(const std::function<void(const Node&)>& f) const
{
    if (!root) return;
    std::vector<const Node*> stack{ root.get() };
    while (!stack.empty())
    {
        const Node* node = stack.back();
        stack.pop_back();
        f(*node);
        if (!node->IsLeaf())
            for (const auto& child : node->children) stack.push_back(child.get());
    }
}
