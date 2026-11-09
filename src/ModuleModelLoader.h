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
    glm::vec3 diffuseColor{ 1.0f };   // color difuso del material (blanco si tiene textura)
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

// Opciones de importado de un modelo (FBX), guardadas en su .meta ("import"). La escala y los ejes se
// aplican en la raiz del .model (no en las mallas): reimportar con otros valores no cambia los .mesh.
struct ModelImportOptions
{
    enum class Axis : uint32_t { YUp = 0, ZUp = 1, Custom = 2 };

    float globalScale = 1.0f;              // multiplica la escala de unidades del archivo
    Axis axis = Axis::YUp;                 // Y arriba no gira; Z arriba gira -90 en X
    glm::vec3 customRotation{ 0.0f };      // grados, si axis == Custom
    bool ignoreCameras = true;
    bool ignoreLights = true;

    // Rotacion (grados, como Transform::rotation) que se aplica en la raiz.
    glm::vec3 AxisRotation() const
    {
        if (axis == Axis::ZUp) return glm::vec3(-90.0f, 0.0f, 0.0f);
        return axis == Axis::Custom ? customRotation : glm::vec3(0.0f);
    }
    bool operator==(const ModelImportOptions&) const = default;
};

// Que es un nodo del modelo, aparte de sus mallas: las camaras y las luces solo se conservan si las
// opciones no las ignoran (sin malla, un nodo se descartaria).
enum class NodeKind : uint32_t { Node = 0, Camera = 1, Light = 2 };

// Nodo de la jerarquia de un modelo: transformacion local (relativa al padre) y las mallas que lleva
// (indices en Model::meshes).
struct ModelNode {
    std::string name;
    glm::mat4 transform{ 1.0f };
    std::vector<unsigned int> meshIndices;
    std::vector<ModelNode> children;
    NodeKind kind = NodeKind::Node;
    // Si kind == Camera (FOV vertical en grados; near y far tal como los da Assimp).
    float cameraFovDeg = 60.0f, cameraNear = 0.1f, cameraFar = 100.0f;
};

struct Model {
    std::vector<Mesh> meshes;   // solo datos de CPU (el importador las guarda en Library), en el espacio local de su nodo
    ModelNode root;
    // Factor de conversion a metros deducido del UnitScaleFactor de los metadatos de Assimp
    // (0.01 si el FBX no lo trae, asumiendo centimetros).
    float unitScale = 0.01f;
    unsigned int cameraCount = 0, lightCount = 0;   // las que trae el archivo (se importen o no)
};

// Lee un modelo con Assimp conservando su jerarquia de nodos. Sin mallas si falla. De las opciones solo
// usa ignoreCameras e ignoreLights (la escala y los ejes los pone el importador en la raiz). Solo lo usa
// el importador (ResourceManager): las escenas se cargan desde Library, sin Assimp.
Model LoadModel(const std::string& path, const ModelImportOptions& options = {});
