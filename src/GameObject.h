#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "Transform.h"
#include "ModuleModelLoader.h"

struct TextureComponent {
    GLuint textureID = 0;
    std::string path;

    bool operator==(const TextureComponent&) const = default;
};

struct MeshComponent {
    MeshPtr mesh;   // nullptr = sin malla (p. ej. un GameObject vacio)

    bool operator==(const MeshComponent&) const = default;
};

// Datos propios de un GameObject, sin el arbol. Se pueden copiar tal cual: Duplicate y los
// snapshots del undo copian esto y reconstruyen el arbol aparte.
struct GameObjectData
{
    enum NormalsMode
    {
        NM_UseGlobal = 0,
        NM_ForceOn,
        NM_ForceOff
    };

    uint64_t uid = 0;               // identificador estable (se serializara en lugar de punteros)
    std::string name;
    Transform transform;            // LOCAL, relativo al padre
    MeshComponent meshComp;
    TextureComponent texComp;

    glm::vec3 aabbMin{ 0.0f };      // AABB local de la malla
    glm::vec3 aabbMax{ 0.0f };

    bool hidden = false;            // oculta tambien a sus hijos (IsHiddenInHierarchy)
    NormalsMode normalsMode = NM_UseGlobal;
    bool showFaceNormals = false;   // normales por cara (triangulo) de este objeto

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

    bool HasMesh() const { return meshComp.mesh != nullptr; }

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

// UID aleatorio de 64 bits, nunca 0 (0 = "ninguno" / la raiz de la escena).
uint64_t GenerateUID();
