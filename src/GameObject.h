#pragma once
#define GLM_ENABLE_EXPERIMENTAL

#include <string>
#include <glad/glad.h>               
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <cfloat>

#include "Transform.h"
#include "ModuleModelLoader.h"


struct TextureComponent {
    GLuint textureID = 0;
    std::string path;
};

struct MeshComponent {
    Mesh mesh;
};

struct GameObject
{
    void UpdateWorldAABB()
    {
        glm::mat4 model = transform.GetMatrix();

        glm::vec3 corners[8] = {
            aabbMin,
            {aabbMax.x, aabbMin.y, aabbMin.z},
            {aabbMin.x, aabbMax.y, aabbMin.z},
            {aabbMax.x, aabbMax.y, aabbMin.z},
            {aabbMin.x, aabbMin.y, aabbMax.z},
            {aabbMax.x, aabbMin.y, aabbMax.z},
            {aabbMin.x, aabbMax.y, aabbMax.z},
            aabbMax
        };

        glm::vec3 minB(FLT_MAX);
        glm::vec3 maxB(-FLT_MAX);

        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 world = glm::vec3(model * glm::vec4(corners[i], 1.0f));
            minB = glm::min(minB, world);
            maxB = glm::max(maxB, world);
        }

        worldAabbMin = minB;
        worldAabbMax = maxB;
    }

    glm::vec3 aabbMin{ 0.0f };
    glm::vec3 aabbMax{ 0.0f };

    glm::vec3 worldAabbMin{ 0.0f };
    glm::vec3 worldAabbMax{ 0.0f };

    // --- Normals debug ---
    enum NormalsMode
    {
        NM_UseGlobal = 0,
        NM_ForceOn,
        NM_ForceOff
    };

    NormalsMode normalsMode = NM_UseGlobal;

    std::string name;
    Transform transform;     
    MeshComponent meshComp;
    TextureComponent texComp;

    bool hidden = false;

    void Draw(GLint uModelLoc) const
    {
        glm::mat4 model = transform.GetMatrix();
        glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));
        meshComp.mesh.Draw();
    }

    void Destroy()
    {
        meshComp.mesh.Destroy();
        texComp.textureID = 0;
    }
};



