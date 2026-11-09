#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>

#include "LibraryFormat.h"
#include "ModuleModelLoader.h"   // Mesh, MeshPtr
#include "TextureLoader.h"

enum class ResourceType { Mesh, Texture, Model };

// Gestor de recursos del motor: importa los assets de Assets/ al formato propio de Library/ y carga
// los recursos desde ahi, una sola vez cada uno aunque los usen muchos GameObjects.
//
// Conteo de referencias: el gestor guarda un weak_ptr de cada recurso cargado y quien lo usa (un
// componente, un snapshot del undo) guarda un shared_ptr. Las referencias son el use_count: al pedir
// un recurso sin referencias se carga desde Library (0 -> 1) y, al soltarse la ultima (1 -> 0), su
// deleter lo descarga de la GPU. Los modelos no se quedan en memoria: solo se leen al instanciarlos.
//
// Cada asset tiene un .meta (JSON) a su lado con su UID y la fecha del archivo importado: si se
// borra Library/, se regenera con los mismos UIDs, y si el asset cambia, se reimporta.
class ResourceManager
{
public:
    struct Entry
    {
        uint64_t uid = 0;
        ResourceType type = ResourceType::Mesh;
        std::string name;           // para mostrar (los de un FBX: "archivo.fbx/malla")
        std::string assetPath;      // relativa a Assets/, con '/'; vacia en los internos (primitivas)
        std::string libraryPath;    // absoluta, UTF-8
        std::weak_ptr<Mesh> mesh;          // si type == Mesh
        std::weak_ptr<Texture> texture;    // si type == Texture

        long RefCount() const { return type == ResourceType::Mesh ? mesh.use_count() : texture.use_count(); }
        bool IsLoaded() const { return RefCount() > 0; }
    };

    // UIDs fijos de las primitivas (internas: se generan por codigo y se guardan en Library).
    static constexpr uint64_t UID_CUBE = 1, UID_SPHERE = 2, UID_CYLINDER = 3, UID_CONE = 4, UID_PLANE = 5;

    // baseDir: carpeta del ejecutable (UTF-8). Assets/ y Library/ van ahi. Crea Library/ si no
    // existe, genera las primitivas e importa todo Assets/.
    void Init(const std::string& baseDir);

    // Recorre Assets/ e importa lo que no este importado o haya cambiado (Init y el boton Refresh).
    void ImportAll();

    // Importa un archivo que esta dentro de Assets/ (si no estaba importado o ha cambiado) y devuelve
    // el UID de su recurso principal: la textura o el modelo. 0 si no es un formato soportado o falla.
    uint64_t ImportAsset(const std::string& absolutePath);

    // Copia un archivo de fuera a la carpeta targetFolder de Assets/ (relativa, "" = la raiz) y lo importa
    // (drag & drop). Si es un FBX, copia tambien las texturas que encuentre junto al original. Si ya hay
    // un archivo con ese nombre en esa carpeta, se usa ese. Devuelve como ImportAsset.
    uint64_t ImportExternalFile(const std::string& absolutePath, const std::string& targetFolder = {});

    // Borra un asset: el archivo, su .meta y sus recursos de Library (un FBX: el modelo y sus mallas).
    // Devuelve los UIDs quitados del registro: quien llama debe soltar las referencias que tenga a ellos.
    std::vector<uint64_t> DeleteAsset(const std::string& assetPath);

    // UID del recurso principal de un asset (ruta relativa a Assets/). 0 si no esta importado.
    uint64_t FindByAssetPath(const std::string& assetPath) const;

    // nullptr si el UID no existe, no es de ese tipo o su archivo de Library no se puede leer.
    MeshPtr GetMesh(uint64_t uid);
    TexturePtr GetTexture(uint64_t uid);
    bool LoadModel(uint64_t uid, ModelNodeData& out) const;

    // Opciones de importado de una textura (ruta relativa a Assets/): las de su .meta, o las de por
    // defecto si no tiene.
    TextureImportOptions GetTextureImportOptions(const std::string& assetPath) const;
    // Guarda las opciones en el .meta y reimporta la textura. Si esta cargada se actualiza en el sitio:
    // el mismo Texture recibe el id de GL nuevo, asi que los materiales y los snapshots del undo no
    // notan nada. false si no es una textura importada o falla.
    bool ApplyTextureImportOptions(const std::string& assetPath, const TextureImportOptions& options);

    // Igual para un modelo (FBX). Reimporta el .model con los mismos UIDs; las instancias que ya estan en
    // la escena no cambian (cada GameObject guarda su propio transform), solo las nuevas.
    ModelImportOptions GetModelImportOptions(const std::string& assetPath) const;
    bool ApplyModelImportOptions(const std::string& assetPath, const ModelImportOptions& options);

    // Lo que trae el FBX segun su ultimo importado (del .meta): nodos del .model, y camaras y luces
    // del archivo (se importen o no).
    struct ModelInfo { int nodes = 0, cameras = 0, lights = 0; };
    ModelInfo GetModelInfo(const std::string& assetPath) const;

    const Entry* Find(uint64_t uid) const;
    const std::map<uint64_t, Entry>& Entries() const { return entries; }
    std::string AssetsDir() const;

    // Ruta relativa a Assets/ (con '/'), o vacia si el archivo no esta dentro de Assets/.
    std::string RelativeToAssets(const std::filesystem::path& file) const;

    // Rutas UTF-8 <-> filesystem (con std::string a secas, Windows usaria la codificacion ANSI).
    static std::filesystem::path PathFromUtf8(const std::string& s);
    static std::string PathToUtf8(const std::filesystem::path& p);

    static ResourceType TypeOfExtension(const std::string& path, bool& supported);

private:
    // forced: reimporta con esas opciones (Apply en el Inspector) aunque este al dia.
    uint64_t ImportTexture(const std::filesystem::path& asset, const TextureImportOptions* forced = nullptr);
    // Si la textura esta cargada, la vuelve a leer de Library en el mismo Texture (nuevo id de GL).
    void ReloadTexture(uint64_t uid);
    uint64_t ImportModel(const std::filesystem::path& asset, const std::filesystem::path& extraTextureDir,
        const ModelImportOptions* forced = nullptr);
    std::filesystem::path LibraryFile(ResourceType type, uint64_t uid) const;
    Entry& Register(uint64_t uid, ResourceType type, const std::string& name, const std::string& assetPath);

    std::filesystem::path assetsDir, libraryDir;
    std::map<uint64_t, Entry> entries;   // ordenado por UID: el panel de recursos sale estable
    std::map<std::string, uint64_t> byAssetPath;
};
