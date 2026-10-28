#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

struct Transform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    glm::mat4 GetMatrix() const
    {
        glm::mat4 mat = glm::mat4(1.0f);
        mat = glm::translate(mat, position);

        mat = mat * glm::yawPitchRoll(
            glm::radians(rotation.y),
            glm::radians(rotation.x),
            glm::radians(rotation.z)
        );

        mat = glm::scale(mat, scale);
        return mat;
    }
};
