#include "LibraryFormat.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace
{
    constexpr uint32_t VERSION = 1;                     // de .mesh (las de .tex y .model estan en el .h)
    constexpr uint32_t VERTEX_FLOATS = 11;              // pos3 normal3 uv2 color3 (ver Mesh::SetupMesh)
    constexpr uint32_t MAX_COUNT = 256u * 1024 * 1024;  // tope de cordura para un archivo corrupto

    constexpr uint32_t Magic(const char (&s)[5])
    {
        return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
    }
    constexpr uint32_t MESH_MAGIC = Magic("AMSH");
    constexpr uint32_t TEX_MAGIC = Magic("ATEX");
    constexpr uint32_t MODEL_MAGIC = Magic("AMDL");

    // Escritura secuencial a un buffer que luego se vuelca de una vez al archivo.
    struct Writer
    {
        std::vector<uint8_t> bytes;

        template <class T> void Put(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto* p = reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(), p, p + sizeof(T));
        }
        template <class T> void PutArray(const std::vector<T>& v)
        {
            Put(uint32_t(v.size()));
            const auto* p = reinterpret_cast<const uint8_t*>(v.data());
            bytes.insert(bytes.end(), p, p + v.size() * sizeof(T));
        }
        void PutString(const std::string& s)
        {
            Put(uint32_t(s.size()));
            bytes.insert(bytes.end(), s.begin(), s.end());
        }
        bool SaveTo(const std::string& path) const
        {
            std::ofstream file(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            return bool(file);
        }
    };

    // Lectura con comprobacion de limites: un archivo truncado o corrupto hace fallar la lectura
    // (ok = false) en lugar de leer fuera del buffer.
    struct Reader
    {
        std::vector<uint8_t> bytes;
        size_t cursor = 0;
        bool ok = true;

        bool Load(const std::string& path) { ok = ReadWholeFile(path, bytes); return ok; }

        template <class T> T Get()
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T value{};
            if (!ok || cursor + sizeof(T) > bytes.size()) { ok = false; return value; }
            std::memcpy(&value, bytes.data() + cursor, sizeof(T));
            cursor += sizeof(T);
            return value;
        }
        template <class T> void GetArray(std::vector<T>& v)
        {
            const uint32_t count = Get<uint32_t>();
            if (!ok || count > MAX_COUNT || cursor + size_t(count) * sizeof(T) > bytes.size()) { ok = false; return; }
            v.resize(count);
            std::memcpy(v.data(), bytes.data() + cursor, size_t(count) * sizeof(T));
            cursor += size_t(count) * sizeof(T);
        }
        std::string GetString()
        {
            std::vector<char> chars;
            GetArray(chars);
            return std::string(chars.begin(), chars.end());
        }
        bool Header(uint32_t magic, uint32_t version = VERSION) { return Get<uint32_t>() == magic && Get<uint32_t>() == version && ok; }
    };

    void PutNode(Writer& w, const ModelNodeData& node)
    {
        w.PutString(node.name);
        w.Put(node.transform);
        w.Put(uint32_t(node.meshes.size()));
        for (const ModelMeshRef& m : node.meshes)
        {
            w.Put(m.meshUid);
            w.Put(m.textureUid);
            w.PutString(m.name);
            w.Put(m.color);
        }
        w.Put(uint32_t(node.children.size()));
        for (const ModelNodeData& child : node.children) PutNode(w, child);
    }

    void GetNode(Reader& r, ModelNodeData& node, int depth)
    {
        if (depth > 256) { r.ok = false; return; }   // un arbol tan profundo solo puede ser basura
        node.name = r.GetString();
        node.transform = r.Get<glm::mat4>();
        const uint32_t meshCount = r.Get<uint32_t>();
        for (uint32_t i = 0; r.ok && i < meshCount; ++i)
        {
            ModelMeshRef m;
            m.meshUid = r.Get<uint64_t>();
            m.textureUid = r.Get<uint64_t>();
            m.name = r.GetString();
            m.color = glm::clamp(r.Get<glm::vec3>(), 0.0f, 1.0f);
            node.meshes.push_back(std::move(m));
        }
        const uint32_t childCount = r.Get<uint32_t>();
        for (uint32_t i = 0; r.ok && i < childCount; ++i)
            GetNode(r, node.children.emplace_back(), depth + 1);
    }
}

bool SaveMeshFile(const std::string& path, const Mesh& mesh)
{
    Writer w;
    w.Put(MESH_MAGIC);
    w.Put(VERSION);
    w.PutString(mesh.name);
    w.PutArray(mesh.vertices);
    w.PutArray(mesh.indices);
    return w.SaveTo(path);
}

bool LoadMeshFile(const std::string& path, Mesh& out)
{
    Reader r;
    if (!r.Load(path) || !r.Header(MESH_MAGIC)) return false;
    out.name = r.GetString();
    r.GetArray(out.vertices);
    r.GetArray(out.indices);
    if (!r.ok || out.vertices.size() % VERTEX_FLOATS != 0) return false;

    // Un indice fuera de rango haria leer a la GPU (y al picking) fuera del buffer de vertices.
    const size_t vertexCount = out.vertices.size() / VERTEX_FLOATS;
    for (unsigned int idx : out.indices)
        if (idx >= vertexCount) return false;
    return true;
}

bool SaveTextureFile(const std::string& path, const TextureData& data)
{
    Writer w;
    w.Put(TEX_MAGIC);
    w.Put(TEX_FORMAT_VERSION);
    w.Put(data.format);
    w.Put(data.width);
    w.Put(data.height);
    w.Put(data.sampler.filter);
    w.Put(data.sampler.wrapS);
    w.Put(data.sampler.wrapT);
    w.Put(uint32_t(data.mips.size()));
    for (const auto& mip : data.mips) w.PutArray(mip);
    return w.SaveTo(path);
}

bool LoadTextureFile(const std::string& path, TextureData& out)
{
    Reader r;
    if (!r.Load(path) || !r.Header(TEX_MAGIC, TEX_FORMAT_VERSION)) return false;   // un .tex v1 se reimporta
    out.format = r.Get<TextureData::Format>();
    out.width = r.Get<uint32_t>();
    out.height = r.Get<uint32_t>();
    out.sampler.filter = r.Get<TextureFilter>();
    out.sampler.wrapS = r.Get<TextureWrap>();
    out.sampler.wrapT = r.Get<TextureWrap>();
    const uint32_t mipCount = r.Get<uint32_t>();
    if (!r.ok || mipCount == 0 || mipCount > 16 || out.width == 0 || out.height == 0 ||
        out.width > 16384 || out.height > 16384 || uint32_t(out.format) > uint32_t(TextureData::Format::DXT5) ||
        uint32_t(out.sampler.filter) > uint32_t(TextureFilter::Nearest) ||
        uint32_t(out.sampler.wrapS) > uint32_t(TextureWrap::Mirrored) || uint32_t(out.sampler.wrapT) > uint32_t(TextureWrap::Mirrored))
        return false;

    out.mips.resize(mipCount);
    for (auto& mip : out.mips) r.GetArray(mip);
    if (!r.ok) return false;

    // Cada nivel tiene que traer todos sus bytes: glTexImage2D leeria w*h*4 aunque el archivo traiga menos.
    uint32_t w = out.width, h = out.height;
    for (const auto& mip : out.mips)
    {
        const size_t blockBytes = out.format == TextureData::Format::DXT1 ? 8 : 16;
        const size_t expected = out.format == TextureData::Format::RGBA8
            ? size_t(w) * h * 4
            : size_t(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * blockBytes;
        if (mip.size() != expected) return false;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return true;
}

bool SaveModelFile(const std::string& path, const ModelNodeData& root)
{
    Writer w;
    w.Put(MODEL_MAGIC);
    w.Put(MODEL_FORMAT_VERSION);
    PutNode(w, root);
    return w.SaveTo(path);
}

bool LoadModelFile(const std::string& path, ModelNodeData& out)
{
    Reader r;
    if (!r.Load(path) || !r.Header(MODEL_MAGIC, MODEL_FORMAT_VERSION)) return false;
    out = ModelNodeData{};
    GetNode(r, out, 0);
    return r.ok;
}
