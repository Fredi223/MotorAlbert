#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

#include "ModuleModelLoader.h"

// Componentes de un GameObject. El Transform no esta aqui: todo GameObject tiene uno y solo uno,
// asi que es un miembro fijo (GameObjectData::transform). El resto son opcionales, como mucho uno
// de cada tipo, y se guardan en una lista (Components) que el Inspector recorre.
//
// Los componentes son datos del motor y no saben dibujarse en el editor: la UI de cada tipo esta
// en PanelInspector.cpp, asi el motor no depende de ImGui.
struct Component
{
    enum class Type { Mesh, Material, Camera, Count };

    virtual ~Component() = default;

    virtual Type GetType() const = 0;
    virtual std::unique_ptr<Component> Clone() const = 0;
    // Compara con otro componente del MISMO tipo (lo comprueba quien llama).
    virtual bool Equals(const Component& other) const = 0;

    bool enabled = true;

    bool operator==(const Component&) const = default;

    static const char* TypeName(Type type)
    {
        switch (type)
        {
        case Type::Mesh:     return "Mesh";
        case Type::Material: return "Material";
        case Type::Camera:   return "Camera";
        default:             return "?";
        }
    }
};

// Implementa GetType, Clone y Equals de un componente concreto a partir de su copia y su ==
// (patron CRTP: Derived es la propia clase que hereda de aqui).
template <class Derived, Component::Type T>
struct ComponentOf : Component
{
    static constexpr Type TYPE = T;

    Type GetType() const override { return T; }
    std::unique_ptr<Component> Clone() const override
    {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
    bool Equals(const Component& other) const override
    {
        return static_cast<const Derived&>(*this) == static_cast<const Derived&>(other);
    }

    bool operator==(const ComponentOf&) const = default;
};

struct ComponentMesh : ComponentOf<ComponentMesh, Component::Type::Mesh>
{
    enum NormalsMode { NM_UseGlobal = 0, NM_ForceOn, NM_ForceOff };

    MeshPtr mesh;                       // compartida entre duplicados y snapshots del undo
    glm::vec3 aabbMin{ 0.0f };          // AABB local de la malla
    glm::vec3 aabbMax{ 0.0f };
    NormalsMode normalsMode = NM_UseGlobal;
    bool showFaceNormals = false;       // normales por cara (triangulo) de este objeto

    bool operator==(const ComponentMesh&) const = default;
};

struct ComponentMaterial : ComponentOf<ComponentMaterial, Component::Type::Material>
{
    GLuint textureID = 0;               // 0 = se dibuja con la checker
    std::string texturePath;

    bool operator==(const ComponentMaterial&) const = default;
};

// Por ahora solo guarda los parametros; la Fase 3 la conecta al render y al culling.
struct ComponentCamera : ComponentOf<ComponentCamera, Component::Type::Camera>
{
    float fovDeg = 60.0f;               // FOV vertical
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    bool operator==(const ComponentCamera&) const = default;
};

// Lista de componentes de un GameObject. Se copia en profundidad (clonando cada componente) y se
// compara componente a componente: por eso GameObjectData sigue siendo copiable y comparable con
// ==, que es lo que usan Duplicate y el undo.
class Components
{
public:
    Components() = default;
    Components(const Components& other)
    {
        list.reserve(other.list.size());
        for (const auto& c : other.list) list.push_back(c->Clone());
    }
    Components& operator=(const Components& other)
    {
        if (this != &other) list = Components(other).list;
        return *this;
    }
    Components(Components&&) noexcept = default;
    Components& operator=(Components&&) noexcept = default;

    bool operator==(const Components& other) const
    {
        return std::equal(list.begin(), list.end(), other.list.begin(), other.list.end(),
            [](const auto& a, const auto& b) { return a->GetType() == b->GetType() && a->Equals(*b); });
    }

    template <class T> T* Get()
    {
        for (const auto& c : list)
            if (c->GetType() == T::TYPE) return static_cast<T*>(c.get());
        return nullptr;
    }
    template <class T> const T* Get() const { return const_cast<Components*>(this)->Get<T>(); }

    bool Has(Component::Type type) const
    {
        return std::any_of(list.begin(), list.end(), [type](const auto& c) { return c->GetType() == type; });
    }

    // Anade un componente nuevo de tipo T, o devuelve el que ya habia (uno por tipo).
    template <class T> T& Add()
    {
        if (T* existing = Get<T>()) return *existing;
        list.push_back(std::make_unique<T>());
        return static_cast<T&>(*list.back());
    }
    // Anade uno de un tipo elegido en tiempo de ejecucion (el menu "Add Component").
    void Add(Component::Type type)
    {
        switch (type)
        {
        case Component::Type::Mesh:     Add<ComponentMesh>(); break;
        case Component::Type::Material: Add<ComponentMaterial>(); break;
        case Component::Type::Camera:   Add<ComponentCamera>(); break;
        default: break;
        }
    }

    void Remove(Component::Type type)
    {
        std::erase_if(list, [type](const auto& c) { return c->GetType() == type; });
    }

    auto begin() const { return list.begin(); }
    auto end() const { return list.end(); }
    size_t size() const { return list.size(); }

private:
    std::vector<std::unique_ptr<Component>> list;
};
