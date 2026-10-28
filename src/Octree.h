#pragma once
#include <vector>
#include <glm/glm.hpp>

struct GameObject;

struct OctreeNode
{
    glm::vec3 min;
    glm::vec3 max;

    std::vector<GameObject*> objects;
    OctreeNode* children[8] = { nullptr };

    bool IsLeaf() const {
        return children[0] == nullptr;
    }
};

class Octree
{
public:
    Octree(const glm::vec3& min, const glm::vec3& max);
    ~Octree();

    void Clear();
    void Rebuild(const glm::vec3& min, const glm::vec3& max);
    void Insert(GameObject* go);

    void Query(const glm::vec3& qMin, const glm::vec3& qMax,
        std::vector<GameObject*>& results) const;

private:
    OctreeNode* root = nullptr;

    const int maxObjects = 4;
    const int maxDepth = 6;

    void Insert(OctreeNode* node, GameObject* go, int depth);
    void Subdivide(OctreeNode* node);

    bool Intersects(const glm::vec3& aMin, const glm::vec3& aMax,
        const glm::vec3& bMin, const glm::vec3& bMax) const;
};
