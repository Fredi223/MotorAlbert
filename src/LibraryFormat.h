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
//   Library/Textures/<uid>.tex   "ATEX", version, formato, ancho, alto, niveles (tamano + bytes)
//   Library/Models/<uid>.model   "AMDL", version, arbol de nodos (nombre, transform, mallas, hijos)
//
// Todas las rutas son UTF-8.

// Una malla dentro de un nodo del modelo: el recurso de malla y el de su textura (0 = sin textura).
struct ModelMeshRef
{
    uint64_t meshUid = 0;
    uint64_t textureUid = 0;
    std::string name;
};

// Nodo de un modelo importado: transformacion local (relativa al padre) y las mallas que lleva.
struct ModelNodeData
{
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<ModelMeshRef> meshes;
    std::vector<ModelNodeData> children;
};

bool SaveMeshFile(const std::string& path, const Mesh& mesh);
bool LoadMeshFile(const std::string& path, Mesh& out);   // solo datos de CPU; falta SetupMesh

bool SaveTextureFile(const std::string& path, const TextureData& data);
bool LoadTextureFile(const std::string& path, TextureData& out);

bool SaveModelFile(const std::string& path, const ModelNodeData& root);
bool LoadModelFile(const std::string& path, ModelNodeData& out);
