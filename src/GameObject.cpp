#include "GameObject.h"

#include <cfloat>

#include <glm/gtc/type_ptr.hpp>

const ComponentMesh* GameObject::ActiveMesh() const
{
    const ComponentMesh* m = components.Get<ComponentMesh>();
    return (m && m->enabled && m->mesh) ? m : nullptr;
}

GLuint GameObject::ActiveTexture() const
{
    const ComponentMaterial* mat = components.Get<ComponentMaterial>();
    return (mat && mat->enabled && mat->texture) ? mat->texture->id : 0;
}

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

    const ComponentMesh* m = ActiveMesh();
    if (!m)
    {
        worldAabbMin = worldAabbMax = glm::vec3(worldMatrix[3]);
        return;
    }

    // AABB de mundo: el que envuelve las 8 esquinas del AABB local ya transformadas.
    const Mesh& mesh = *m->mesh;
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec3 corner((i & 1) ? mesh.aabbMax.x : mesh.aabbMin.x,
                               (i & 2) ? mesh.aabbMax.y : mesh.aabbMin.y,
                               (i & 4) ? mesh.aabbMax.z : mesh.aabbMin.z);
        const glm::vec3 world = glm::vec3(worldMatrix * glm::vec4(corner, 1.0f));
        minB = glm::min(minB, world);
        maxB = glm::max(maxB, world);
    }
    worldAabbMin = minB;
    worldAabbMax = maxB;
}

void GameObject::Draw(GLint uModelLoc) const
{
    const ComponentMesh* m = ActiveMesh();
    if (!m) return;
    glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(worldMatrix));
    m->mesh->Draw();
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
