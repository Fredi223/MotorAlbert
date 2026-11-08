#include "GameObject.h"

#include <cfloat>
#include <random>
#include <glm/gtc/type_ptr.hpp>

bool GameObject::IsHiddenInHierarchy() const
{
    for (const GameObject* go = this; go; go = go->parent)
        if (go->hidden) return true;
    return false;
}

bool GameObject::IsDescendantOf(const GameObject* ancestor) const
{
    for (const GameObject* go = parent; go; go = go->parent)
        if (go == ancestor) return true;
    return false;
}

glm::mat4 GameObject::ComputeWorldMatrix() const
{
    const glm::mat4 local = transform.GetMatrix();
    return parent ? parent->ComputeWorldMatrix() * local : local;
}

void GameObject::UpdateWorld(const glm::mat4& parentWorld)
{
    worldMatrix = parentWorld * transform.GetMatrix();

    if (!HasMesh())
    {
        worldAabbMin = worldAabbMax = glm::vec3(worldMatrix[3]);
        return;
    }

    // AABB de mundo: el que envuelve las 8 esquinas del AABB local ya transformadas.
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec3 corner((i & 1) ? aabbMax.x : aabbMin.x,
                               (i & 2) ? aabbMax.y : aabbMin.y,
                               (i & 4) ? aabbMax.z : aabbMin.z);
        const glm::vec3 world = glm::vec3(worldMatrix * glm::vec4(corner, 1.0f));
        minB = glm::min(minB, world);
        maxB = glm::max(maxB, world);
    }
    worldAabbMin = minB;
    worldAabbMax = maxB;
}

void GameObject::Draw(GLint uModelLoc) const
{
    if (!meshComp.mesh) return;
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(worldMatrix));
    meshComp.mesh->Draw();
}

static void GrowHierarchyAABB(const GameObject& go, glm::vec3& minB, glm::vec3& maxB, bool& any)
{
    if (go.HasMesh())
    {
        minB = glm::min(minB, go.worldAabbMin);
        maxB = glm::max(maxB, go.worldAabbMax);
        any = true;
    }
    for (const auto& child : go.children)
        GrowHierarchyAABB(*child, minB, maxB, any);
}

bool ComputeHierarchyAABB(const GameObject& go, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);
    bool any = false;
    GrowHierarchyAABB(go, outMin, outMax, any);
    if (!any)
        outMin = outMax = glm::vec3(go.worldMatrix[3]);
    return any;
}

uint64_t GenerateUID()
{
    static std::mt19937_64 rng{ std::random_device{}() };
    uint64_t uid = 0;
    while (uid == 0) uid = rng();
    return uid;
}
