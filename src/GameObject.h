#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Component.h"
#include "Transform.h"
#include "Uid.h"

// Datos propios de un GameObject, sin el arbol. Se pueden copiar tal cual (los componentes se
// clonan): Duplicate y los snapshots del undo copian esto y reconstruyen el arbol aparte.
struct GameObjectData
{
    uint64_t uid = 0;               // identificador estable (se serializara en lugar de punteros)
    std::string name;
    Transform transform;            // LOCAL, relativo al padre. Todo GameObject tiene uno.
    Components components;          // Mesh, Material, Camera... como mucho uno de cada tipo

    bool hidden = false;            // oculta tambien a sus hijos (IsHiddenInHierarchy)

    bool operator==(const GameObjectData&) const = default;
};

// Nodo de la escena. Cada GameObject es dueno de sus hijos (unique_ptr), asi que borrar uno borra
// su rama entera y sus direcciones no cambian al anadir o quitar hermanos. parent no es dueno.
struct GameObject : GameObjectData
{
    GameObject* parent = nullptr;
    std::vector<std::unique_ptr<GameObject>> children;

    // Derivados del transform: los recalcula Scene::UpdateTransforms.
    glm::mat4 worldMatrix{ 1.0f };
    glm::vec3 worldAabbMin{ 0.0f };   // sin malla: el punto de la posicion de mundo
    glm::vec3 worldAabbMax{ 0.0f };

    // La malla que cuenta para dibujar, el picking, el octree y los AABB: la del componente Mesh si
    // existe, esta activo y tiene malla. nullptr en cualquier otro caso.
    const ComponentMesh* ActiveMesh() const;
    bool HasMesh() const { return ActiveMesh() != nullptr; }

    // Textura con la que se dibuja: la del componente Material si existe y esta activo; 0 si no
    // (el renderer pone entonces la checker).
    GLuint ActiveTexture() const;

    // true si el o alguno de sus antecesores esta oculto.
    bool IsHiddenInHierarchy() const;

    // true si ancestor es su padre, abuelo... (un objeto no es descendiente de si mismo).
    bool IsDescendantOf(const GameObject* ancestor) const;

    // Matriz de mundo calculada subiendo por los padres (no usa la cacheada).
    glm::mat4 ComputeWorldMatrix() const;

    // Recalcula worldMatrix y el AABB de mundo a partir de la matriz de mundo del padre.
    void UpdateWorld(const glm::mat4& parentWorld);

    void Draw(GLint uModelLoc) const;
};

// AABB de mundo que engloba las mallas del objeto y de toda su rama. Si ninguna tiene malla devuelve
// false y la caja es el punto de su posicion de mundo.
bool ComputeHierarchyAABB(const GameObject& go, glm::vec3& outMin, glm::vec3& outMax);
