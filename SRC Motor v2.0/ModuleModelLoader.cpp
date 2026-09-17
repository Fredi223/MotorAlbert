#include "ModuleModelLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>
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

void CalculateMeshAABB(const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);

    for (size_t i = 0; i < mesh.vertices.size(); i += 11)
    {
        glm::vec3 v(
            mesh.vertices[i + 0],
            mesh.vertices[i + 1],
            mesh.vertices[i + 2]
        );

        outMin = glm::min(outMin, v);
        outMax = glm::max(outMax, v);
    }
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

    // posición
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

    glBegin(GL_LINES);
    for (size_t i = 0; i < vertices.size(); i += 11)
    {
        glm::vec3 pos(vertices[i + 0], vertices[i + 1], vertices[i + 2]);
        glm::vec3 norm(vertices[i + 3], vertices[i + 4], vertices[i + 5]);
        glm::vec3 end = pos + norm * 0.1f; 

        glVertex3fv(glm::value_ptr(pos));
        glVertex3fv(glm::value_ptr(end));
    }
    glEnd();
        
    glBindVertexArray(0);
}


void Mesh::Destroy()
{
    if (EBO) { glDeleteBuffers(1, &EBO); EBO = 0; }
    if (VBO) { glDeleteBuffers(1, &VBO); VBO = 0; }
    if (VAO) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
    if (textureID) { glDeleteTextures(1, &textureID); textureID = 0; }

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

    // Transformación del nodo (CLAVE)
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

    // --- Vértices ---
    mesh.vertices.reserve(ai_mesh->mNumVertices * 11);

    for (unsigned int v = 0; v < ai_mesh->mNumVertices; ++v)
    {
        // POSICIÓN (con transformación aplicada)
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
            mesh.vertices.push_back(1.0f - ai_mesh->mTextureCoords[0][v].y);
        }
        else
        {
            mesh.vertices.insert(mesh.vertices.end(), { 0.f, 0.f });
        }

        // COLOR
        mesh.vertices.insert(mesh.vertices.end(), { 1.f, 1.f, 1.f });
    }

    // --- Índices ---
    for (unsigned int f = 0; f < ai_mesh->mNumFaces; ++f)
    {
        const aiFace& face = ai_mesh->mFaces[f];
        for (unsigned int i = 0; i < face.mNumIndices; ++i)
            mesh.indices.push_back(face.mIndices[i]);
    }

    mesh.SetupMesh();
    outMeshes.push_back(std::move(mesh));
}


std::vector<Mesh> LoadModel(const std::string& path)
{
    std::vector<Mesh> outMeshes;
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes |

        aiProcess_ConvertToLeftHanded |
        aiProcess_PreTransformVertices
    );




    if (!scene) {
        Console::LogError("Assimp error: %s", importer.GetErrorString());
        return outMeshes;
    }

    if (!scene->HasMeshes()) {
        std::cerr << "No meshes in scene\n";
        return outMeshes;
    }

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
