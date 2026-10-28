#include "ModuleModelLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>
#include <algorithm>
#include <cfloat>
#include "stb_image.h"

#include "Logger.h" 

glm::mat4 AiToGlm(const aiMatrix4x4& m)
{
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

void Mesh::SetupMesh()
{
    if (vertices.empty() || indices.empty()) return;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Formato: pos(3) normal(3) tex(2) color(3) = 11 * sizeof(float)
    GLsizei stride = 11 * sizeof(float);

    // posici�n
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)(0));
    glEnableVertexAttribArray(0);

    // normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // texcoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // color
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    
    glBindVertexArray(0);

    indexCount = static_cast<unsigned int>(indices.size());
    const int VERTEX_STRIDE_FLOATS = 11;
    vertexCount = static_cast<unsigned int>(vertices.size() / VERTEX_STRIDE_FLOATS);

    // --- Normales por cara: centroide + normal geometrica de cada triangulo ---
    auto position = [&](unsigned int idx) {
        const size_t o = static_cast<size_t>(idx) * VERTEX_STRIDE_FLOATS;
        return glm::vec3(vertices[o], vertices[o + 1], vertices[o + 2]);
        };
    auto vertexNormal = [&](unsigned int idx) {
        const size_t o = static_cast<size_t>(idx) * VERTEX_STRIDE_FLOATS + 3;
        return glm::vec3(vertices[o], vertices[o + 1], vertices[o + 2]);
        };

    std::vector<float> faceData;
    faceData.reserve(indices.size() / 3 * 6);
    for (size_t t = 0; t + 2 < indices.size(); t += 3)
    {
        const unsigned int i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        const glm::vec3 p0 = position(i0), p1 = position(i1), p2 = position(i2);

        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float len = glm::length(n);
        if (len < 1e-12f) continue;   // triangulo degenerado: no tiene normal
        n /= len;

        // Si el orden de los vertices da una normal contraria a la de los vertices (modelo con
        // winding invertido), se gira para que coincida con la que se usa para iluminar.
        if (glm::dot(n, vertexNormal(i0) + vertexNormal(i1) + vertexNormal(i2)) < 0.0f) n = -n;

        const glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
        faceData.insert(faceData.end(), { centroid.x, centroid.y, centroid.z, n.x, n.y, n.z });
    }
    faceNormalCount = static_cast<unsigned int>(faceData.size() / 6);

    glGenVertexArrays(1, &faceNormalVAO);
    glGenBuffers(1, &faceNormalVBO);
    glBindVertexArray(faceNormalVAO);
    glBindBuffer(GL_ARRAY_BUFFER, faceNormalVBO);
    glBufferData(GL_ARRAY_BUFFER, faceData.size() * sizeof(float), faceData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Mesh::Draw() const
{
    if (VAO == 0) return;
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Mesh::DrawNormals() const
{
    if (VAO == 0) return;

    glBindVertexArray(VAO);
    glDrawArrays(GL_POINTS, 0, (GLsizei)vertexCount);
    glBindVertexArray(0);
}

void Mesh::DrawFaceNormals() const
{
    if (faceNormalVAO == 0) return;

    glBindVertexArray(faceNormalVAO);
    glDrawArrays(GL_POINTS, 0, (GLsizei)faceNormalCount);
    glBindVertexArray(0);
}


void Mesh::Destroy()
{
    if (EBO) { glDeleteBuffers(1, &EBO); EBO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (faceNormalVBO) { glDeleteBuffers(1, &faceNormalVBO); faceNormalVBO = 0; }
    if (faceNormalVAO) { glDeleteVertexArrays(1, &faceNormalVAO); faceNormalVAO = 0; }
    faceNormalCount = 0;
    // La textura NO se borra aqui: varios GameObject pueden compartir el mismo GLuint
    // (p. ej. un duplicado) y borrarla dejaria a los demas con un handle invalido.
    // La liberacion la decide quien sabe quien la usa (ver EraseGameObjectAt en main.cpp).
    textureID = 0;

    vertices.clear();
    indices.clear();
}

static void ProcessAIMesh(
    const aiMesh* ai_mesh,
    const aiScene* scene,
    const aiNode* node,
    const std::string& modelPath,
    std::vector<Mesh>& outMeshes)
{
    Mesh mesh;

    // Transformaci�n del nodo (CLAVE)
    glm::mat4 nodeTransform = AiToGlm(node->mTransformation);

    // --- Material / textura ---
    if (ai_mesh->mMaterialIndex >= 0)
    {
        aiMaterial* material = scene->mMaterials[ai_mesh->mMaterialIndex];
        aiString path;
        if (AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &path))
        {
            mesh.texturePath = path.C_Str();
        }
    }

    // --- V�rtices ---
    mesh.vertices.reserve(ai_mesh->mNumVertices * 11);

    // Si la malla no trae UV (p. ej. los exports de Unity sin mapeado), se generan por proyeccion
    // de caja: para cada vertice se usa el plano perpendicular al eje dominante de su normal, y
    // la textura se reparte una vez por el lado mayor de la malla. Hace falta el minimo y la
    // extension de la malla, que se calculan antes.
    const bool hasUVs = ai_mesh->mTextureCoords[0] != nullptr;
    glm::vec3 posMin(FLT_MAX), posMax(-FLT_MAX);
    if (!hasUVs)
    {
        for (unsigned int v = 0; v < ai_mesh->mNumVertices; ++v)
        {
            const glm::vec3 p = glm::vec3(nodeTransform * glm::vec4(
                ai_mesh->mVertices[v].x, ai_mesh->mVertices[v].y, ai_mesh->mVertices[v].z, 1.0f));
            posMin = glm::min(posMin, p);
            posMax = glm::max(posMax, p);
        }
    }
    const glm::vec3 posSize = posMax - posMin;
    const float uvExtent = std::max(std::max(posSize.x, std::max(posSize.y, posSize.z)), 1e-6f);

    for (unsigned int v = 0; v < ai_mesh->mNumVertices; ++v)
    {
        // POSICI�N (con transformaci�n aplicada)
        glm::vec4 p = nodeTransform * glm::vec4(
            ai_mesh->mVertices[v].x,
            ai_mesh->mVertices[v].y,
            ai_mesh->mVertices[v].z,
            1.0f
        );

        mesh.vertices.push_back(p.x);
        mesh.vertices.push_back(p.y);
        mesh.vertices.push_back(p.z);

        // NORMAL
        if (ai_mesh->HasNormals())
        {
            mesh.vertices.push_back(ai_mesh->mNormals[v].x);
            mesh.vertices.push_back(ai_mesh->mNormals[v].y);
            mesh.vertices.push_back(ai_mesh->mNormals[v].z);
        }
        else
        {
            mesh.vertices.insert(mesh.vertices.end(), { 0.f, 1.f, 0.f });
        }

        // UV
        if (ai_mesh->mTextureCoords[0])
        {
            mesh.vertices.push_back(ai_mesh->mTextureCoords[0][v].x);
            // La V de FBX ya tiene el origen abajo, como OpenGL; la imagen se voltea al cargarla
            // (stbi_set_flip_vertically_on_load). Antes aqui se invertia otra vez para compensar el
            // flip que aiProcess_ConvertToLeftHanded hacia por dentro; sin ese flag sobra.
            mesh.vertices.push_back(ai_mesh->mTextureCoords[0][v].y);
        }
        else
        {
            const glm::vec3 n = ai_mesh->HasNormals()
                ? glm::vec3(ai_mesh->mNormals[v].x, ai_mesh->mNormals[v].y, ai_mesh->mNormals[v].z)
                : glm::vec3(0.f, 1.f, 0.f);
            const glm::vec3 a = glm::abs(n);
            float u, w;
            if (a.x >= a.y && a.x >= a.z) { u = (p.z - posMin.z) / uvExtent; w = (p.y - posMin.y) / uvExtent; }
            else if (a.y >= a.z)          { u = (p.x - posMin.x) / uvExtent; w = (p.z - posMin.z) / uvExtent; }
            else                          { u = (p.x - posMin.x) / uvExtent; w = (p.y - posMin.y) / uvExtent; }
            mesh.vertices.insert(mesh.vertices.end(), { u, w });
        }

        // COLOR
        mesh.vertices.insert(mesh.vertices.end(), { 1.f, 1.f, 1.f });
    }

    // --- �ndices ---
    for (unsigned int f = 0; f < ai_mesh->mNumFaces; ++f)
    {
        const aiFace& face = ai_mesh->mFaces[f];
        for (unsigned int i = 0; i < face.mNumIndices; ++i)
            mesh.indices.push_back(face.mIndices[i]);
    }

    mesh.SetupMesh();
    Console::LogInfo("Assimp: malla '%s' -> %zu vertices, %zu indices, textura difusa: %s",
        ai_mesh->mName.C_Str(), mesh.vertices.size() / 11, mesh.indices.size(),
        mesh.texturePath.empty() ? "ninguna" : mesh.texturePath.c_str());
    outMeshes.push_back(std::move(mesh));
}


std::vector<Mesh> LoadModel(const std::string& path, float* outUnitScale)
{
    std::vector<Mesh> outMeshes;
    Assimp::Importer importer;

    Console::LogInfo("Assimp: importando %s", path.c_str());
    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes |
        aiProcess_PreTransformVertices
    );




    if (!scene) {
        Console::LogError("Assimp error: %s", importer.GetErrorString());
        return outMeshes;
    }

    if (outUnitScale)
    {
        // UnitScaleFactor (FBX): centimetros por unidad nativa del archivo.
        // Si no hay metadata, se asume 1.0 (cm), el caso mas comun.
        // Assimp guarda UnitScaleFactor como float; Get<double> falla en silencio por el tipo y
        // dejaba siempre el valor por defecto (los modelos con otra unidad salian mal escalados).
        float unitScaleFactor = 1.0f;
        if (scene->mMetaData)
        {
            double asDouble = 0.0;
            if (!scene->mMetaData->Get("UnitScaleFactor", unitScaleFactor) &&
                scene->mMetaData->Get("UnitScaleFactor", asDouble))
            {
                unitScaleFactor = static_cast<float>(asDouble);
            }
        }
        *outUnitScale = unitScaleFactor / 100.0f;
    }

    if (!scene->HasMeshes()) {
        Console::LogError("Assimp: %s no contiene mallas", path.c_str());
        return outMeshes;
    }

    Console::LogInfo("Assimp: %u mallas, %u materiales", scene->mNumMeshes, scene->mNumMaterials);
    outMeshes.reserve(scene->mNumMeshes);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        ProcessAIMesh(
            scene->mMeshes[i],
            scene,
            scene->mRootNode,
            path,
            outMeshes
        );


    }
    return outMeshes;
}
