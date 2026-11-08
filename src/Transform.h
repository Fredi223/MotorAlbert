#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

// Transform LOCAL de un GameObject (relativo a su padre). Rotacion en grados, aplicada como
// yaw (Y), pitch (X) y roll (Z): glm::yawPitchRoll, que es la misma matriz que eulerAngleYXZ.
struct Transform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    bool operator==(const Transform&) const = default;

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

    // Inversa de GetMatrix: descompone una matriz TRS en posicion, rotacion y escala. Si la matriz
    // tiene cizalla (padre con escala no uniforme y un hijo girado) se pierde: TRS no la representa.
    void SetFromMatrix(const glm::mat4& m)
    {
        position = glm::vec3(m[3]);

        glm::vec3 axes[3] = { glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2]) };
        for (int i = 0; i < 3; ++i)
            scale[i] = glm::max(glm::length(axes[i]), 1e-6f);

        // Una matriz con determinante negativo es un espejo: se guarda como escala X negativa.
        if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f)
            scale.x = -scale.x;

        glm::mat4 rot(1.0f);
        for (int i = 0; i < 3; ++i)
            rot[i] = glm::vec4(axes[i] / scale[i], 0.0f);

        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        glm::extractEulerAngleYXZ(rot, yaw, pitch, roll);
        rotation = glm::degrees(glm::vec3(pitch, yaw, roll));
    }
};
