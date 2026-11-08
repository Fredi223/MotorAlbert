#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

struct Mesh {
    uint64_t uid = 0;          // UID del recurso (0 = malla suelta, sin pasar por el ResourceManager)
    glm::vec3 aabbMin{ 0.0f }; // AABB local, lo calcula SetupMesh
    glm::vec3 aabbMax{ 0.0f };
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    std::string name;          // nombre de la malla en el archivo (vacio en las primitivas)
    std::string texturePath;   // ruta de la textura difusa tal como la guarda el material del FBX
    unsigned int indexCount = 0;
    unsigned int vertexCount = 0;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    // Normales por cara: un punto por triangulo (centroide + normal, 6 floats) que se dibuja
    // con el mismo shader que las normales por vertice. Los crea SetupMesh.
    GLuint faceNormalVAO = 0;
    GLuint faceNormalVBO = 0;
    unsigned int faceNormalCount = 0;

    void SetupMesh();
    void Draw() const;
    void DrawNormals() const;
    void DrawFaceNormals() const;
    void Destroy();
};

// Malla compartida: la poseen los GameObject que la usan (duplicados incluidos) y los snapshots del
// undo. Sus buffers de GPU se liberan cuando la suelta el ultimo (MakeSharedMesh pone ese deleter).
using MeshPtr = std::shared_ptr<Mesh>;
MeshPtr MakeSharedMesh(Mesh&& mesh);

// Nodo de la jerarquia de un modelo: transformacion local (relativa al padre) y las mallas que lleva
// (indices en Model::meshes).
struct ModelNode {
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<unsigned int> meshIndices;
    std::vector<ModelNode> children;
};

struct Model {
    std::vector<Mesh> meshes;   // solo datos de CPU (el importador las guarda en Library), en el espacio local de su nodo
    ModelNode root;
    // Factor de conversion a metros deducido del UnitScaleFactor de los metadatos de Assimp
    // (0.01 si el FBX no lo trae, asumiendo centimetros).
    float unitScale = 0.01f;
};

// Lee un modelo con Assimp conservando su jerarquia de nodos. Sin mallas si falla. Solo lo usa el
// importador (ResourceManager): las escenas se cargan desde Library, sin Assimp.
Model LoadModel(const std::string& path);
