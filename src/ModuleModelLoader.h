#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>

struct Mesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
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

// outUnitScale (opcional): factor de conversion a metros deducido del
// UnitScaleFactor de los metadatos de Assimp (fallback 0.01 si el FBX
// no trae esa metadata, asumiendo centimetros).
std::vector<Mesh> LoadModel(const std::string& path, float* outUnitScale = nullptr);
