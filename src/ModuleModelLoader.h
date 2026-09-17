#pragma once

#include <string>
#include <vector>
#include <glad/glad.h>

struct Mesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint EBO = 0;
    GLuint textureID = 0;
    std::string texturePath;
    unsigned int indexCount = 0;
    unsigned int vertexCount = 0;
    std::vector<float> vertices; 
    std::vector<unsigned int> indices;

    void SetupMesh();   
    void Draw() const;  
    void DrawNormals() const;
    void Destroy();     
};
// outUnitScale (opcional): factor de conversion a metros deducido del
// UnitScaleFactor de los metadatos de Assimp (fallback 0.01 si el FBX
// no trae esa metadata, asumiendo centimetros).
std::vector<Mesh> LoadModel(const std::string& path, float* outUnitScale = nullptr);
