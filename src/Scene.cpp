#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <unordered_map>

#include "Geometry.h"
#include "Logger.h"
#include "TextureLoader.h"

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
        // Sin mallas no hay limites reales (degenerarian a +-infinito): se usan los mismos limites
        // fijos que la consulta de dibujado.
        minB = glm::vec3(-1000.0f);
        maxB = glm::vec3(1000.0f);
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

    Console::LogInfo("Octree rebuilt (%zu objects with mesh)", meshCount);
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

// Lo que comparten los nodos de un modelo al convertirlos en GameObjects: cada malla (y su textura)
// se crea una vez aunque la usen varios nodos.
struct LoadedMesh {
    MeshPtr mesh;
    glm::vec3 aabbMin{ 0.0f }, aabbMax{ 0.0f };
    GLuint texture = 0;
    std::string texturePath;
};

static void SetMeshOn(GameObject& go, const LoadedMesh& m)
{
    go.meshComp.mesh = m.mesh;
    go.aabbMin = m.aabbMin;
    go.aabbMax = m.aabbMax;
    go.texComp.textureID = m.texture;
    go.texComp.path = m.texturePath;
}

// Un nodo con una sola malla la lleva el mismo; con varias, cada una va en un hijo (un GameObject
// tiene una malla).
static void AttachMeshes(Scene& scene, GameObject& go, const ModelNode& node, const std::vector<LoadedMesh>& meshes)
{
    if (node.meshIndices.size() == 1)
    {
        SetMeshOn(go, meshes[node.meshIndices[0]]);
        return;
    }
    for (unsigned int idx : node.meshIndices)
    {
        const std::string& meshName = meshes[idx].mesh->name;
        GameObject* part = scene.CreateEmpty(meshName.empty() ? node.name + "_mesh" : meshName, &go);
        SetMeshOn(*part, meshes[idx]);
    }
}

static void BuildNode(Scene& scene, const ModelNode& node, GameObject* parent, const std::vector<LoadedMesh>& meshes)
{
    GameObject* go = scene.CreateEmpty(node.name, parent);
    go->transform.SetFromMatrix(node.transform);
    AttachMeshes(scene, *go, node, meshes);
    for (const ModelNode& child : node.children)
        BuildNode(scene, child, go, meshes);
}

GameObject* Scene::LoadFbx(const std::string& filePath)
{
    Model model = LoadModel(filePath);
    if (model.meshes.empty()) return nullptr;

    const size_t lastSlash = filePath.find_last_of("/\\");
    const size_t nameStart = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;
    const size_t lastDot = filePath.find_last_of('.');
    const std::string baseName = filePath.substr(nameStart,
        (lastDot == std::string::npos || lastDot < nameStart ? filePath.size() : lastDot) - nameStart);
    const std::string fbxDir = filePath.substr(0, nameStart);

    // BakerHouse.fbx es inconsistente: sus vertices ya vienen con Y arriba, pero su nodo
    // lleva ademas una rotacion de -90 en X (pensada para modelos con Z arriba, como el
    // Hotdog). Con esa rotacion la casa queda tumbada, con el tejado hacia -Z; +90 en X en la
    // raiz la deja derecha. No hay regla general que distinga este caso, asi que se corrige por nombre.
    std::string lowerBase = baseName;
    std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(),
        [](unsigned char c) { return std::tolower(c); });
    const bool isBakerHouse = (lowerBase == "bakerhouse");

    // Mallas y texturas, una vez cada una. Una textura la pueden usar varias mallas.
    std::vector<LoadedMesh> meshes;
    meshes.reserve(model.meshes.size());
    std::unordered_map<std::string, GLuint> texturesByPath;
    for (Mesh& mesh : model.meshes)
    {
        LoadedMesh m;
        m.mesh = MakeSharedMesh(std::move(mesh));
        ComputeAABBFromMesh(*m.mesh, m.aabbMin, m.aabbMax);
        m.texture = defaultTexture;

        const std::string& storedPath = m.mesh->texturePath;
        if (!storedPath.empty())
        {
            // La ruta del material puede ser solo el nombre de archivo o la ruta absoluta del
            // PC donde se exporto el FBX (que aqui no existe): se usa solo el nombre del archivo,
            // buscado junto al .fbx (no en el directorio de trabajo del proceso).
            const size_t texNameStart = storedPath.find_last_of("/\\");
            const std::string textureName = (texNameStart == std::string::npos) ? storedPath : storedPath.substr(texNameStart + 1);
            const std::string textureFullPath = fbxDir + textureName;

            auto cached = texturesByPath.find(textureFullPath);
            if (cached == texturesByPath.end())
            {
                const GLuint tex = LoadImageTexture(textureFullPath);
                if (!tex) Console::LogError("Se usa la textura checker en lugar de %s", textureFullPath.c_str());
                cached = texturesByPath.emplace(textureFullPath, tex).first;
            }
            if (cached->second)
            {
                m.texture = cached->second;
                m.texturePath = textureFullPath;
            }
        }
        meshes.push_back(std::move(m));
    }

    // La raiz del modelo es el nodo raiz del archivo, con la escala de unidades del FBX (y el giro de
    // la Baker House) por encima: mover, girar o escalar esta raiz afecta al modelo entero.
    Transform fix;
    fix.scale = glm::vec3(model.unitScale);
    if (isBakerHouse) fix.rotation.x = 90.0f;

    GameObject* modelRoot = CreateEmpty(baseName);
    modelRoot->transform.SetFromMatrix(fix.GetMatrix() * model.root.transform);
    AttachMeshes(*this, *modelRoot, model.root, meshes);
    for (const ModelNode& child : model.root.children)
        BuildNode(*this, child, modelRoot, meshes);

    RebuildOctree();
    Console::LogInfo("FBX cargado: %s (%zu mallas, %zu texturas)", filePath.c_str(), meshes.size(), texturesByPath.size());
    return modelRoot;
}

GameObject* Scene::CreateEmpty(const std::string& name, GameObject* parent)
{
    auto go = std::make_unique<GameObject>();
    go->uid = GenerateUID();
    go->name = name;
    return AddChild(parent, std::move(go));
}

GameObject* Scene::AddPrimitive(const char* name, Mesh mesh)
{
    auto go = std::make_unique<GameObject>();
    go->uid = GenerateUID();
    go->name = name;
    go->meshComp.mesh = MakeSharedMesh(std::move(mesh));
    ComputeAABBFromMesh(*go->meshComp.mesh, go->aabbMin, go->aabbMax);
    go->texComp.textureID = defaultTexture;

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

    // Las texturas de la rama se liberan cuando ya no esta en la escena (si nadie mas las usa).
    std::vector<GLuint> textures;
    std::function<void(const GameObject&)> collect = [&](const GameObject& o) {
        textures.push_back(o.texComp.textureID);
        for (const auto& child : o.children) collect(*child);
    };
    collect(*go);

    go->parent->children.erase(FindInParent(go));   // destruye la rama entera
    structureDirty = true;

    std::sort(textures.begin(), textures.end());
    textures.erase(std::unique(textures.begin(), textures.end()), textures.end());
    for (GLuint tex : textures) ReleaseTextureIfUnused(tex);
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

void Scene::ReleaseTextureIfUnused(GLuint tex)
{
    if (tex == 0 || tex == defaultTexture) return;
    bool inUse = false;
    ForEach([&](const GameObject& o) { if (o.texComp.textureID == tex) inUse = true; });
    if (!inUse && isTextureUsedElsewhere) inUse = isTextureUsedElsewhere(tex);
    if (!inUse) glDeleteTextures(1, &tex);
}
