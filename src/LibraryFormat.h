#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "ModuleModelLoader.h"   // Mesh
#include "TextureLoader.h"       // TextureData

// Formato propio de los recursos importados (lo que hay en Library/). Son binarios con una cabecera
// (identificador de 4 letras + version) seguida de los datos tal cual se usan en memoria, asi que
// cargarlos es leer y copiar: no hace falta Assimp ni stb_image.
//
//   Library/Meshes/<uid>.mesh    "AMSH", version, nombre, vertices (11 floats cada uno), indices
//   Library/Textures/<uid>.tex   "ATEX", version, formato, ancho, alto, filtro, wrap S y T, niveles (tamano + bytes)
//   Library/Models/<uid>.model   "AMDL", version, arbol de nodos (nombre, transform, tipo [+ camara], mallas, hijos)
//
// Todas las rutas son UTF-8.

// Version de los .model. La 2 anade el color del material a cada malla y la 3 el tipo de nodo (camara
// o luz, con los datos de la camara). El .meta del modelo la guarda ("format") para reimportar los
// .model de una version anterior.
constexpr uint32_t MODEL_FORMAT_VERSION = 3;

// Version de los .tex. La 2 anade el sampler (filtro y wrap, opciones de importado). El .meta de la
// textura tambien la guarda ("format") para reimportar los .tex v1 con el mismo UID.
constexpr uint32_t TEX_FORMAT_VERSION = 2;

// Una malla dentro de un nodo del modelo: el recurso de malla, el de su textura (0 = sin textura)
// y el color del material.
struct ModelMeshRef
{
    uint64_t meshUid = 0;
    uint64_t textureUid = 0;
    std::string name;
    glm::vec3 color{ 1.0f };
};

// Nodo de un modelo importado: transformacion local (relativa al padre) y las mallas que lleva.
struct ModelNodeData
{
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<ModelMeshRef> meshes;
    std::vector<ModelNodeData> children;
    NodeKind kind = NodeKind::Node;
    float cameraFovDeg = 60.0f, cameraNear = 0.1f, cameraFar = 100.0f;   // si kind == Camera
};

bool SaveMeshFile(const std::string& path, const Mesh& mesh);
bool LoadMeshFile(const std::string& path, Mesh& out);   // solo datos de CPU; falta SetupMesh

bool SaveTextureFile(const std::string& path, const TextureData& data);
bool LoadTextureFile(const std::string& path, TextureData& out);

bool SaveModelFile(const std::string& path, const ModelNodeData& root);
bool LoadModelFile(const std::string& path, ModelNodeData& out);
