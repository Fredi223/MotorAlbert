#include "ResourceManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>
#include <nlohmann/json.hpp>

#include "Logger.h"
#include "Primitives.h"
#include "Transform.h"
#include "Uid.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    // Rutas en UTF-8 <-> std::filesystem::path. Con std::string a secas, Windows interpretaria la
    // ruta en la codificacion ANSI y una carpeta con tildes no se encontraria.
    fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
    std::string ToUtf8(const fs::path& p)
    {
        const std::u8string u = p.u8string();
        return std::string(u.begin(), u.end());
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    // Fecha de modificacion como entero, para compararla con la guardada en el .meta.
    int64_t FileStamp(const fs::path& p)
    {
        std::error_code ec;
        const auto t = fs::last_write_time(p, ec);
        return ec ? 0 : int64_t(t.time_since_epoch().count());
    }

    fs::path MetaPath(const fs::path& asset) { return fs::path(asset).concat(".meta"); }

    json ReadMeta(const fs::path& asset)
    {
        std::ifstream file(MetaPath(asset));
        if (!file) return json::object();
        json meta = json::parse(file, nullptr, false);   // sin excepciones: un .meta roto se ignora
        return meta.is_object() ? meta : json::object();
    }

    bool WriteMeta(const fs::path& asset, const json& meta)
    {
        std::ofstream file(MetaPath(asset), std::ios::trunc);
        file << meta.dump(2) << '\n';
        return bool(file);
    }

    uint64_t MetaUid(const json& meta)
    {
        const auto it = meta.find("uid");
        return (it != meta.end() && it->is_number_unsigned()) ? it->get<uint64_t>() : 0;
    }
}

ResourceType ResourceManager::TypeOfExtension(const std::string& path, bool& supported)
{
    const std::string ext = Lower(FromUtf8(path).extension().string());
    supported = true;
    if (ext == ".fbx") return ResourceType::Model;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".dds")
        return ResourceType::Texture;
    supported = false;
    return ResourceType::Mesh;
}

std::string ResourceManager::AssetsDir() const { return ToUtf8(assetsDir); }

std::string ResourceManager::RelativeToAssets(const fs::path& file) const
{
    const fs::path rel = fs::weakly_canonical(file).lexically_relative(fs::weakly_canonical(assetsDir));
    if (rel.empty() || *rel.begin() == "..") return {};
    const std::u8string u = rel.generic_u8string();
    return std::string(u.begin(), u.end());
}

fs::path ResourceManager::LibraryFile(ResourceType type, uint64_t uid) const
{
    switch (type)
    {
    case ResourceType::Mesh:    return libraryDir / "Meshes" / (std::to_string(uid) + ".mesh");
    case ResourceType::Texture: return libraryDir / "Textures" / (std::to_string(uid) + ".tex");
    default:                    return libraryDir / "Models" / (std::to_string(uid) + ".model");
    }
}

ResourceManager::Entry& ResourceManager::Register(uint64_t uid, ResourceType type, const std::string& name, const std::string& assetPath)
{
    // Si ya existia (reimportar), se conservan sus weak_ptr: lo que este cargado sigue valiendo.
    Entry& e = entries[uid];
    e.uid = uid;
    e.type = type;
    e.name = name;
    e.assetPath = assetPath;
    e.libraryPath = ToUtf8(LibraryFile(type, uid));
    return e;
}

void ResourceManager::Init(const std::string& baseDir)
{
    assetsDir = FromUtf8(baseDir) / "Assets";
    libraryDir = FromUtf8(baseDir) / "Library";
    std::error_code ec;
    for (const char* sub : { "Meshes", "Textures", "Models" })
        fs::create_directories(libraryDir / sub, ec);
    fs::create_directories(assetsDir, ec);

    // Primitivas: recursos internos con UID fijo. Se generan una vez y luego se cargan de Library.
    struct Builtin { uint64_t uid; const char* name; Mesh (*create)(); };
    const Builtin builtins[] = {
        { UID_CUBE, "Cube", [] { return CreateCubeMesh(); } },
        { UID_SPHERE, "Sphere", [] { return CreateSphereMesh(); } },
        { UID_CYLINDER, "Cylinder", [] { return CreateCylinderMesh(); } },
        { UID_CONE, "Cone", [] { return CreateConeMesh(); } },
        { UID_PLANE, "Plane", [] { return CreatePlaneMesh(); } },
    };
    for (const Builtin& b : builtins)
    {
        Entry& e = Register(b.uid, ResourceType::Mesh, std::string(b.name) + " (built-in)", "");
        if (!fs::exists(LibraryFile(ResourceType::Mesh, b.uid)))
        {
            Mesh mesh = b.create();
            mesh.name = b.name;
            SaveMeshFile(e.libraryPath, mesh);
        }
    }

    ImportAll();
}

void ResourceManager::ImportAll()
{
    // Todo Assets/: primero las texturas, para que los FBX encuentren las suyas ya importadas.
    std::error_code ec;
    std::vector<fs::path> textures, models;
    for (const auto& item : fs::recursive_directory_iterator(assetsDir, fs::directory_options::skip_permission_denied, ec))
    {
        if (!item.is_regular_file()) continue;
        bool supported = false;
        const ResourceType type = TypeOfExtension(ToUtf8(item.path()), supported);
        if (!supported) continue;
        (type == ResourceType::Texture ? textures : models).push_back(item.path());
    }
    for (const fs::path& p : textures) ImportTexture(p);
    for (const fs::path& p : models) ImportModel(p, {});

    Console::LogInfo("Recursos: %zu registrados (%zu texturas y %zu modelos en Assets/)", entries.size(), textures.size(), models.size());
}

uint64_t ResourceManager::ImportAsset(const std::string& absolutePath)
{
    bool supported = false;
    const ResourceType type = TypeOfExtension(absolutePath, supported);
    if (!supported)
    {
        Console::LogError("Formato no soportado: %s", absolutePath.c_str());
        return 0;
    }
    const fs::path path = FromUtf8(absolutePath);
    return type == ResourceType::Texture ? ImportTexture(path) : ImportModel(path, {});
}

uint64_t ResourceManager::ImportExternalFile(const std::string& absolutePath, const std::string& targetFolder)
{
    const fs::path source = FromUtf8(absolutePath);
    if (!RelativeToAssets(source).empty())
        return ImportAsset(absolutePath);   // ya esta dentro de Assets/

    bool supported = false;
    const ResourceType type = TypeOfExtension(absolutePath, supported);
    if (!supported)
    {
        Console::LogError("Formato no soportado: %s", absolutePath.c_str());
        return 0;
    }

    std::error_code ec;
    const fs::path folder = assetsDir / FromUtf8(targetFolder);
    fs::create_directories(folder, ec);
    const fs::path target = folder / source.filename();
    const std::string targetRel = RelativeToAssets(target);
    if (fs::exists(target))
        Console::LogInfo("Ya hay un Assets/%s: se usa ese", targetRel.c_str());
    else if (!fs::copy_file(source, target, ec))
    {
        Console::LogError("No se pudo copiar %s a Assets/%s (%s)", absolutePath.c_str(), targetFolder.c_str(), ec.message().c_str());
        return 0;
    }
    else
        Console::LogInfo("Copiado a Assets/%s", targetRel.c_str());

    return type == ResourceType::Texture ? ImportTexture(target) : ImportModel(target, source.parent_path());
}

uint64_t ResourceManager::ImportTexture(const fs::path& asset)
{
    const std::string rel = RelativeToAssets(asset);
    json meta = ReadMeta(asset);
    uint64_t uid = MetaUid(meta);
    if (uid == 0) uid = GenerateUID();

    const fs::path lib = LibraryFile(ResourceType::Texture, uid);
    const int64_t stamp = FileStamp(asset);
    const bool upToDate = MetaUid(meta) != 0 && meta.value("timestamp", int64_t(0)) == stamp && fs::exists(lib);
    if (!upToDate)
    {
        TextureData data;
        if (!DecodeImageFile(ToUtf8(asset), data) || !SaveTextureFile(ToUtf8(lib), data))
        {
            Console::LogError("No se pudo importar la textura %s", rel.c_str());
            return 0;
        }
        meta = { { "uid", uid }, { "type", "texture" }, { "timestamp", stamp } };
        WriteMeta(asset, meta);
        Console::LogInfo("Importada textura %s -> Library/Textures/%llu.tex", rel.c_str(), (unsigned long long)uid);
    }

    Register(uid, ResourceType::Texture, rel, rel);
    byAssetPath[rel] = uid;
    return uid;
}

// Convierte el arbol de nodos de Assimp al del .model, con los UIDs de cada malla y de su textura.
static void ConvertNode(const ModelNode& node, const std::vector<uint64_t>& meshUids,
    const std::vector<uint64_t>& textureUids, const std::vector<Mesh>& meshes, ModelNodeData& out)
{
    out.name = node.name;
    out.transform = node.transform;
    for (unsigned int idx : node.meshIndices)
        out.meshes.push_back({ meshUids[idx], textureUids[idx], meshes[idx].name, meshes[idx].diffuseColor });
    for (const ModelNode& child : node.children)
        ConvertNode(child, meshUids, textureUids, meshes, out.children.emplace_back());
}

uint64_t ResourceManager::ImportModel(const fs::path& asset, const fs::path& extraTextureDir)
{
    const std::string rel = RelativeToAssets(asset);
    const std::string fileName = ToUtf8(asset.filename());
    json meta = ReadMeta(asset);
    uint64_t modelUid = MetaUid(meta);
    if (modelUid == 0) modelUid = GenerateUID();

    // UIDs de las mallas guardados en el .meta, por orden: reimportar conserva los mismos.
    std::vector<uint64_t> meshUids;
    std::vector<std::string> meshNames;
    if (meta.contains("meshes") && meta["meshes"].is_array())
        for (const json& m : meta["meshes"])
        {
            meshUids.push_back(MetaUid(m));
            meshNames.push_back(m.value("name", std::string()));
        }

    const int64_t stamp = FileStamp(asset);
    bool upToDate = MetaUid(meta) != 0 && meta.value("timestamp", int64_t(0)) == stamp &&
        meta.value("format", uint32_t(0)) == MODEL_FORMAT_VERSION &&   // un .model antiguo se rehace
        fs::exists(LibraryFile(ResourceType::Model, modelUid));
    for (uint64_t uid : meshUids)
        upToDate = upToDate && uid != 0 && fs::exists(LibraryFile(ResourceType::Mesh, uid));

    if (!upToDate)
    {
        Model model = ::LoadModel(ToUtf8(asset));
        if (model.meshes.empty())
        {
            Console::LogError("No se pudo importar el modelo %s", rel.c_str());
            return 0;
        }

        meshUids.resize(model.meshes.size(), 0);
        meshNames.assign(model.meshes.size(), {});
        std::vector<uint64_t> textureUids(model.meshes.size(), 0);
        std::map<std::string, uint64_t> texturesByName;   // una textura la pueden usar varias mallas
        const fs::path assetDir = asset.parent_path();

        for (size_t i = 0; i < model.meshes.size(); ++i)
        {
            Mesh& mesh = model.meshes[i];
            if (meshUids[i] == 0) meshUids[i] = GenerateUID();
            meshNames[i] = mesh.name;
            SaveMeshFile(ToUtf8(LibraryFile(ResourceType::Mesh, meshUids[i])), mesh);

            if (mesh.texturePath.empty()) continue;
            // La ruta del material puede ser solo el nombre de archivo o la ruta absoluta del PC donde
            // se exporto el FBX (que aqui no existe): se busca solo el nombre, junto al .fbx.
            const fs::path texName = FromUtf8(mesh.texturePath).filename();
            auto cached = texturesByName.find(ToUtf8(texName));
            if (cached == texturesByName.end())
            {
                fs::path texAsset = assetDir / texName;
                std::error_code ec;
                // Arrastrado desde fuera: la textura se trae a Assets/ junto al FBX.
                if (!fs::exists(texAsset) && !extraTextureDir.empty() && fs::exists(extraTextureDir / texName) &&
                    fs::copy_file(extraTextureDir / texName, texAsset, ec))
                    Console::LogInfo("Copiado a Assets/: %s", ToUtf8(texName).c_str());

                uint64_t texUid = 0;
                if (fs::exists(texAsset)) texUid = ImportTexture(texAsset);
                if (texUid == 0) Console::LogError("Textura no encontrada junto a %s: %s (se usa la checker)", fileName.c_str(), ToUtf8(texName).c_str());
                cached = texturesByName.emplace(ToUtf8(texName), texUid).first;
            }
            textureUids[i] = cached->second;
        }

        // BakerHouse.fbx es inconsistente: sus vertices ya vienen con Y arriba, pero su nodo lleva
        // ademas una rotacion de -90 en X (pensada para modelos con Z arriba). Con esa rotacion la
        // casa queda tumbada; +90 en X en la raiz la deja derecha. No hay regla general que
        // distinga este caso, asi que se corrige por nombre.
        Transform fix;
        fix.scale = glm::vec3(model.unitScale);
        if (Lower(ToUtf8(asset.stem())) == "bakerhouse") fix.rotation.x = 90.0f;

        // La raiz del .model lleva el nombre del archivo y la escala de unidades (y el giro de la
        // Baker House) por encima del nodo raiz: mover o escalar esa raiz afecta al modelo entero.
        ModelNodeData root;
        ConvertNode(model.root, meshUids, textureUids, model.meshes, root);
        root.name = ToUtf8(asset.stem());
        root.transform = fix.GetMatrix() * root.transform;
        SaveModelFile(ToUtf8(LibraryFile(ResourceType::Model, modelUid)), root);

        json metaMeshes = json::array();
        for (size_t i = 0; i < meshUids.size(); ++i)
            metaMeshes.push_back({ { "uid", meshUids[i] }, { "name", meshNames[i] } });
        meta = { { "uid", modelUid }, { "type", "model" }, { "format", MODEL_FORMAT_VERSION }, { "timestamp", stamp }, { "meshes", metaMeshes } };
        WriteMeta(asset, meta);
        Console::LogInfo("Importado modelo %s -> Library/Models/%llu.model (%zu mallas)", rel.c_str(),
            (unsigned long long)modelUid, meshUids.size());
    }

    Register(modelUid, ResourceType::Model, rel, rel);
    for (size_t i = 0; i < meshUids.size(); ++i)
        Register(meshUids[i], ResourceType::Mesh, rel + "/" + meshNames[i], rel);
    byAssetPath[rel] = modelUid;
    return modelUid;
}

uint64_t ResourceManager::FindByAssetPath(const std::string& assetPath) const
{
    const auto it = byAssetPath.find(assetPath);
    return it != byAssetPath.end() ? it->second : 0;
}

const ResourceManager::Entry* ResourceManager::Find(uint64_t uid) const
{
    const auto it = entries.find(uid);
    return it != entries.end() ? &it->second : nullptr;
}

MeshPtr ResourceManager::GetMesh(uint64_t uid)
{
    const auto it = entries.find(uid);
    if (it == entries.end() || it->second.type != ResourceType::Mesh) return nullptr;
    Entry& e = it->second;
    if (MeshPtr loaded = e.mesh.lock()) return loaded;

    Mesh mesh;
    if (!LoadMeshFile(e.libraryPath, mesh))
    {
        Console::LogError("No se pudo leer la malla %s (%s)", e.name.c_str(), e.libraryPath.c_str());
        return nullptr;
    }
    mesh.uid = uid;
    mesh.SetupMesh();

    // El deleter es la descarga: se ejecuta al soltarse la ultima referencia.
    MeshPtr ptr(new Mesh(std::move(mesh)), [name = e.name](Mesh* m) {
        m->Destroy();
        delete m;
        Console::LogInfo("Recurso descargado (0 referencias): malla %s", name.c_str());
    });
    e.mesh = ptr;
    Console::LogInfo("Recurso cargado desde Library: malla %s", e.name.c_str());
    return ptr;
}

TexturePtr ResourceManager::GetTexture(uint64_t uid)
{
    const auto it = entries.find(uid);
    if (it == entries.end() || it->second.type != ResourceType::Texture) return nullptr;
    Entry& e = it->second;
    if (TexturePtr loaded = e.texture.lock()) return loaded;

    TextureData data;
    if (!LoadTextureFile(e.libraryPath, data))
    {
        Console::LogError("No se pudo leer la textura %s (%s)", e.name.c_str(), e.libraryPath.c_str());
        return nullptr;
    }
    auto texture = new Texture{ uid, UploadTexture(data), data.width, data.height, data.format };
    TexturePtr ptr(texture, [name = e.name](Texture* t) {
        glDeleteTextures(1, &t->id);
        delete t;
        Console::LogInfo("Recurso descargado (0 referencias): textura %s", name.c_str());
    });
    e.texture = ptr;
    Console::LogInfo("Recurso cargado desde Library: textura %s (%ux%u)", e.name.c_str(), data.width, data.height);
    return ptr;
}

bool ResourceManager::LoadModel(uint64_t uid, ModelNodeData& out) const
{
    const Entry* e = Find(uid);
    if (!e || e->type != ResourceType::Model) return false;
    if (!LoadModelFile(e->libraryPath, out))
    {
        Console::LogError("No se pudo leer el modelo %s (%s)", e->name.c_str(), e->libraryPath.c_str());
        return false;
    }
    return true;
}

std::vector<uint64_t> ResourceManager::DeleteAsset(const std::string& assetPath)
{
    std::vector<uint64_t> removed;
    if (assetPath.empty()) return removed;   // los internos (primitivas) no se pueden borrar

    // Sus recursos (un FBX tiene el modelo y sus mallas) fuera del registro y de Library. Lo que este
    // cargado sigue vivo mientras alguien lo use: quien llama debe soltarlo (los GameObjects y el undo).
    std::error_code ec;
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (it->second.assetPath != assetPath) { ++it; continue; }
        fs::remove(FromUtf8(it->second.libraryPath), ec);
        removed.push_back(it->first);
        it = entries.erase(it);
    }
    byAssetPath.erase(assetPath);

    const fs::path asset = assetsDir / FromUtf8(assetPath);
    fs::remove(asset, ec);
    fs::remove(MetaPath(asset), ec);
    if (fs::exists(asset))
        Console::LogError("No se pudo borrar Assets/%s", assetPath.c_str());
    else
        Console::LogInfo("Asset borrado: Assets/%s (%zu recursos fuera de Library)", assetPath.c_str(), removed.size());
    return removed;
}

fs::path ResourceManager::PathFromUtf8(const std::string& s) { return FromUtf8(s); }
std::string ResourceManager::PathToUtf8(const fs::path& p) { return ToUtf8(p); }
