#include "Scene.h"

#include <algorithm>
#include <cfloat>

#include "Geometry.h"
#include "Logger.h"
#include "ResourceManager.h"

Scene::Scene()
{
    root.name = "Scene";   // uid 0: los hijos de primer nivel tienen como padre "0"
}

// Busca el unique_ptr que posee a go dentro de los hijos de su padre.
static std::vector<std::unique_ptr<GameObject>>::iterator FindInParent(GameObject* go)
{
    auto& siblings = go->parent->children;
    return std::find_if(siblings.begin(), siblings.end(),
        [go](const std::unique_ptr<GameObject>& c) { return c.get() == go; });
}

GameObject* Scene::FindByUid(uint64_t uid)
{
    if (uid == 0) return nullptr;
    GameObject* found = nullptr;
    ForEach([&](GameObject& go) { if (go.uid == uid) found = &go; });
    return found;
}

const GameObject* Scene::FindCullingCamera() const
{
    const GameObject* found = nullptr;
    ForEach([&found](const GameObject& go) {
        const auto* cam = go.components.Get<ComponentCamera>();
        if (!found && cam && cam->enabled && cam->cullingCamera && !go.IsHiddenInHierarchy()) found = &go;
    });
    return found;
}

GameObject* Scene::CreateCamera(const glm::mat4& worldTransform)
{
    GameObject* go = CreateEmpty("Camera");
    go->transform.SetFromMatrix(worldTransform);
    go->components.Add<ComponentCamera>();
    go->UpdateWorld(glm::mat4(1.0f));
    return go;
}

static void UpdateWorldRecursive(GameObject& go, const glm::mat4& parentWorld)
{
    go.UpdateWorld(parentWorld);
    for (auto& child : go.children)
        UpdateWorldRecursive(*child, go.worldMatrix);
}

void Scene::UpdateTransforms()
{
    // La raiz siempre es la identidad, asi que su worldMatrix es la de partida de los hijos.
    for (auto& child : root.children)
        UpdateWorldRecursive(*child, glm::mat4(1.0f));
}

void Scene::RebuildOctree()
{
    UpdateTransforms();

    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    size_t meshCount = 0;
    ForEach([&](const GameObject& go) {
        if (!go.HasMesh()) return;
        minB = glm::min(minB, go.worldAabbMin);
        maxB = glm::max(maxB, go.worldAabbMax);
        ++meshCount;
    });

    if (meshCount == 0)
    {
        // Sin mallas no hay limites reales (degenerarian a +-infinito): da igual cuales, una caja unidad.
        minB = glm::vec3(-1.0f);
        maxB = glm::vec3(1.0f);
    }
    else
    {
        // 10% de holgura para que un objeto en el borde no quede justo en el limite.
        const glm::vec3 margin = (maxB - minB) * 0.1f;
        minB -= margin;
        maxB += margin;
    }

    octree.Rebuild(minB, maxB);
    ForEach([&](GameObject& go) { if (go.HasMesh()) octree.Insert(&go); });
    // Sin log: se reconstruye en cada frame en que algo se mueve (arrastrar en el Inspector).
}

GameObject* Scene::AddChild(GameObject* parent, std::unique_ptr<GameObject> child)
{
    if (!parent) parent = &root;
    child->parent = parent;
    GameObject* raw = child.get();
    parent->children.push_back(std::move(child));
    raw->UpdateWorld(parent->ComputeWorldMatrix());
    structureDirty = true;
    return raw;
}

// Pone en go la malla y la textura de una referencia del modelo, pidiendolas al gestor (que carga
// cada una una sola vez aunque la usen varios nodos).
static void SetMeshOn(GameObject& go, const ModelMeshRef& ref, ResourceManager& resources)
{
    go.components.Add<ComponentMesh>().mesh = resources.GetMesh(ref.meshUid);
    ComponentMaterial& material = go.components.Add<ComponentMaterial>();
    material.texture = ref.textureUid ? resources.GetTexture(ref.textureUid) : nullptr;
    material.color = ref.color;
}

// Un nodo con una sola malla la lleva el mismo; con varias, cada una va en un hijo (un GameObject
// tiene una malla).
static void BuildNode(Scene& scene, const ModelNodeData& node, GameObject& go, ResourceManager& resources)
{
    go.transform.SetFromMatrix(node.transform);
    if (node.meshes.size() == 1)
        SetMeshOn(go, node.meshes[0], resources);
    else
        for (const ModelMeshRef& ref : node.meshes)
            SetMeshOn(*scene.CreateEmpty(ref.name.empty() ? node.name + "_mesh" : ref.name, &go), ref, resources);

    for (const ModelNodeData& child : node.children)
        BuildNode(scene, child, *scene.CreateEmpty(child.name, &go), resources);
}

GameObject* Scene::InstantiateModel(uint64_t modelUid, ResourceManager& resources)
{
    ModelNodeData model;
    if (!resources.LoadModel(modelUid, model)) return nullptr;

    GameObject* modelRoot = CreateEmpty(model.name);
    BuildNode(*this, model, *modelRoot, resources);
    RebuildOctree();
    Console::LogInfo("Modelo instanciado: %s", model.name.c_str());
    return modelRoot;
}

GameObject* Scene::CreateEmpty(const std::string& name, GameObject* parent)
{
    auto go = std::make_unique<GameObject>();
    go->uid = GenerateUID();
    go->name = name;
    return AddChild(parent, std::move(go));
}

GameObject* Scene::AddPrimitive(const char* name, MeshPtr mesh)
{
    if (!mesh) return nullptr;
    auto go = std::make_unique<GameObject>();
    go->uid = GenerateUID();
    go->name = name;
    go->components.Add<ComponentMesh>().mesh = std::move(mesh);
    go->components.Add<ComponentMaterial>();   // sin textura: la checker

    GameObject* raw = AddChild(nullptr, std::move(go));
    RebuildOctree();
    Console::LogInfo("Geometria basica creada: %s", name);
    return raw;
}

// Copia profunda de una rama: datos copiados (mallas y texturas compartidas), UIDs nuevos.
static std::unique_ptr<GameObject> CloneTree(const GameObject& src, GameObject* parent)
{
    auto copy = std::make_unique<GameObject>();
    static_cast<GameObjectData&>(*copy) = src;
    copy->uid = GenerateUID();
    copy->parent = parent;
    copy->worldMatrix = src.worldMatrix;
    copy->worldAabbMin = src.worldAabbMin;
    copy->worldAabbMax = src.worldAabbMax;
    for (const auto& child : src.children)
        copy->children.push_back(CloneTree(*child, copy.get()));
    return copy;
}

GameObject* Scene::Duplicate(GameObject* go)
{
    if (!go || go == &root) return nullptr;

    auto copy = CloneTree(*go, go->parent);
    copy->name += "_copy";
    copy->transform.position += glm::vec3(0.1f, 0.0f, 0.1f);

    GameObject* raw = copy.get();
    auto& siblings = go->parent->children;
    siblings.insert(FindInParent(go) + 1, std::move(copy));
    structureDirty = true;
    return raw;
}

void Scene::Erase(GameObject* go)
{
    if (!go || go == &root) return;

    // Destruye la rama entera. Sus mallas y texturas se descargan solas si nadie mas las usa.
    go->parent->children.erase(FindInParent(go));
    structureDirty = true;
}

bool Scene::Reparent(GameObject* go, GameObject* newParent)
{
    if (!newParent) newParent = &root;
    if (!go || go == &root || go->parent == newParent || newParent == go || newParent->IsDescendantOf(go))
        return false;

    // Conservar la posicion en el mundo: local nuevo = inversa(mundo del padre nuevo) * mundo actual.
    const glm::mat4 world = go->ComputeWorldMatrix();
    go->transform.SetFromMatrix(glm::inverse(newParent->ComputeWorldMatrix()) * world);

    auto it = FindInParent(go);
    std::unique_ptr<GameObject> owned = std::move(*it);
    go->parent->children.erase(it);
    owned->parent = newParent;
    newParent->children.push_back(std::move(owned));

    structureDirty = true;
    return true;
}

void Scene::Clear()
{
    root.children.clear();
    structureDirty = true;
}

