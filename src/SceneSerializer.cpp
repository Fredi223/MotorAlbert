#include "SceneSerializer.h"

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

#include "Logger.h"
#include "ResourceManager.h"
#include "Scene.h"

using json = nlohmann::json;

namespace
{
    constexpr int SCENE_VERSION = 1;

    json Vec3ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

    glm::vec3 Vec3FromJson(const json& j, const glm::vec3& fallback)
    {
        if (!j.is_array() || j.size() != 3) return fallback;
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    }

    json ComponentToJson(const Component& c)
    {
        json j = { { "type", Component::TypeName(c.GetType()) }, { "enabled", c.enabled } };
        switch (c.GetType())
        {
        case Component::Type::Mesh:
        {
            const auto& mesh = static_cast<const ComponentMesh&>(c);
            j["mesh"] = mesh.mesh ? mesh.mesh->uid : 0;
            j["normalsMode"] = static_cast<int>(mesh.normalsMode);
            j["showFaceNormals"] = mesh.showFaceNormals;
            break;
        }
        case Component::Type::Material:
        {
            const auto& material = static_cast<const ComponentMaterial&>(c);
            j["texture"] = material.texture ? material.texture->uid : 0;
            j["color"] = Vec3ToJson(material.color);
            break;
        }
        case Component::Type::Camera:
        {
            const auto& cam = static_cast<const ComponentCamera&>(c);
            j["fov"] = cam.fovDeg;
            j["near"] = cam.nearPlane;
            j["far"] = cam.farPlane;
            j["cullingCamera"] = cam.cullingCamera;
            break;
        }
        case Component::Type::Rotator:
            j["degreesPerSecond"] = Vec3ToJson(static_cast<const ComponentRotator&>(c).degreesPerSecond);
            break;
        default: break;
        }
        return j;
    }

    // Anade a go el componente descrito en j. Los recursos que falten se avisan y el componente queda vacio.
    void ComponentFromJson(const json& j, GameObject& go, ResourceManager& resources)
    {
        const std::string type = j.at("type").get<std::string>();
        Component* added = nullptr;

        if (type == Component::TypeName(Component::Type::Mesh))
        {
            auto& mesh = go.components.Add<ComponentMesh>();
            if (const uint64_t uid = j.value("mesh", uint64_t{ 0 }))
            {
                mesh.mesh = resources.GetMesh(uid);
                if (!mesh.mesh) Console::LogError("Escena: %s usa una malla que no existe (UID %llu)", go.name.c_str(), (unsigned long long)uid);
            }
            const int mode = j.value("normalsMode", 0);
            mesh.normalsMode = (mode >= 0 && mode <= ComponentMesh::NM_ForceOff) ? static_cast<ComponentMesh::NormalsMode>(mode) : ComponentMesh::NM_UseGlobal;
            mesh.showFaceNormals = j.value("showFaceNormals", false);
            added = &mesh;
        }
        else if (type == Component::TypeName(Component::Type::Material))
        {
            auto& material = go.components.Add<ComponentMaterial>();
            if (const uint64_t uid = j.value("texture", uint64_t{ 0 }))
            {
                material.texture = resources.GetTexture(uid);
                if (!material.texture) Console::LogError("Escena: %s usa una textura que no existe (UID %llu)", go.name.c_str(), (unsigned long long)uid);
            }
            material.color = glm::clamp(Vec3FromJson(j.value("color", json()), glm::vec3(1.0f)), 0.0f, 1.0f);
            added = &material;
        }
        else if (type == Component::TypeName(Component::Type::Camera))
        {
            auto& cam = go.components.Add<ComponentCamera>();
            const ComponentCamera defaults;
            cam.fovDeg = j.value("fov", defaults.fovDeg);
            cam.nearPlane = j.value("near", defaults.nearPlane);
            cam.farPlane = j.value("far", defaults.farPlane);
            cam.cullingCamera = j.value("cullingCamera", false);
            added = &cam;
        }
        else if (type == Component::TypeName(Component::Type::Rotator))
        {
            auto& rotator = go.components.Add<ComponentRotator>();
            rotator.degreesPerSecond = Vec3FromJson(j.value("degreesPerSecond", json()), ComponentRotator{}.degreesPerSecond);
            added = &rotator;
        }
        else
        {
            Console::LogError("Escena: componente desconocido \"%s\" en %s (se ignora)", type.c_str(), go.name.c_str());
            return;
        }
        added->enabled = j.value("enabled", true);
    }
}

json SceneSerializer::ToJson(const Scene& scene)
{
    json objects = json::array();
    scene.ForEach([&objects](const GameObject& go) {
        json components = json::array();
        for (const auto& c : go.components) components.push_back(ComponentToJson(*c));

        objects.push_back({
            { "uid", go.uid },
            { "parent", go.parent ? go.parent->uid : 0 },
            { "name", go.name },
            { "hidden", go.hidden },
            { "transform", {
                { "position", Vec3ToJson(go.transform.position) },
                { "rotation", Vec3ToJson(go.transform.rotation) },
                { "scale", Vec3ToJson(go.transform.scale) } } },
            { "components", std::move(components) },
        });
    });
    return { { "version", SCENE_VERSION }, { "gameObjects", std::move(objects) } };
}

bool SceneSerializer::FromJson(const json& data, Scene& scene, ResourceManager& resources)
{
    // Primero se construyen los objetos aparte; la escena solo se toca si todo el JSON es valido.
    // Asi tambien se reutilizan los recursos que la escena actual ya tiene cargados (no se descargan
    // y se vuelven a cargar).
    struct Loaded { std::unique_ptr<GameObject> go; uint64_t parentUid; };
    std::vector<Loaded> loaded;
    try
    {
        if (!data.is_object() || !data.contains("gameObjects")) throw std::runtime_error("no es una escena");
        const int version = data.value("version", 0);
        if (version != SCENE_VERSION) throw std::runtime_error("version " + std::to_string(version) + " no soportada");

        std::unordered_set<uint64_t> usedUids;
        for (const json& j : data.at("gameObjects"))
        {
            auto go = std::make_unique<GameObject>();
            go->uid = j.at("uid").get<uint64_t>();
            go->name = j.value("name", std::string("GameObject"));
            // UID 0 (la raiz) o repetido: el objeto se carga con uno nuevo.
            if (go->uid == 0 || !usedUids.insert(go->uid).second)
            {
                go->uid = GenerateUID();
                usedUids.insert(go->uid);
                Console::LogError("Escena: %s tenia un UID invalido o repetido (se le asigna otro)", go->name.c_str());
            }
            go->hidden = j.value("hidden", false);

            const json& t = j.value("transform", json::object());
            go->transform.position = Vec3FromJson(t.value("position", json()), glm::vec3(0.0f));
            go->transform.rotation = Vec3FromJson(t.value("rotation", json()), glm::vec3(0.0f));
            go->transform.scale = Vec3FromJson(t.value("scale", json()), glm::vec3(1.0f));

            for (const json& c : j.value("components", json::array()))
                ComponentFromJson(c, *go, resources);

            loaded.push_back({ std::move(go), j.value("parent", uint64_t{ 0 }) });
        }
    }
    catch (const std::exception& e)
    {
        Console::LogError("No se pudo cargar la escena: %s", e.what());
        return false;
    }

    // Reconstruir el arbol. Preorden: el padre de cada objeto ya esta colocado cuando llega el objeto.
    scene.Clear();
    std::unordered_map<uint64_t, GameObject*> byUid{ { 0, &scene.root } };
    for (Loaded& item : loaded)
    {
        auto parentIt = byUid.find(item.parentUid);
        if (parentIt == byUid.end())
        {
            Console::LogError("Escena: el padre de %s no existe (queda en el primer nivel)", item.go->name.c_str());
            parentIt = byUid.find(0);
        }
        GameObject* parent = parentIt->second;
        item.go->parent = parent;
        byUid[item.go->uid] = item.go.get();
        parent->children.push_back(std::move(item.go));
    }
    scene.RebuildOctree();   // tambien actualiza las matrices de mundo
    scene.structureDirty = false;
    return true;
}

bool SceneSerializer::SaveToFile(const Scene& scene, const std::filesystem::path& file)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (!out)
    {
        Console::LogError("No se pudo escribir la escena: %s", ResourceManager::PathToUtf8(file).c_str());
        return false;
    }
    out << ToJson(scene).dump(2);
    return static_cast<bool>(out);
}

bool SceneSerializer::LoadFromFile(const std::filesystem::path& file, Scene& scene, ResourceManager& resources)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        Console::LogError("No se pudo abrir la escena: %s", ResourceManager::PathToUtf8(file).c_str());
        return false;
    }
    const json data = json::parse(in, nullptr, false);   // sin excepciones: un archivo roto da discarded
    if (data.is_discarded())
    {
        Console::LogError("La escena no es un JSON valido: %s", ResourceManager::PathToUtf8(file).c_str());
        return false;
    }
    return FromJson(data, scene, resources);
}
