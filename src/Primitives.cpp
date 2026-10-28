#include "Primitives.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace
{
    void AddVertex(Mesh& mesh, const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv)
    {
        mesh.vertices.insert(mesh.vertices.end(), { p.x, p.y, p.z, n.x, n.y, n.z, uv.x, uv.y, 1.0f, 1.0f, 1.0f });
    }

    // Cuadrado de lado 1 centrado en "center", en el plano generado por u y v. Con u x v = normal
    // los triangulos quedan en sentido antihorario visto desde la normal.
    void AddQuad(Mesh& mesh, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v, const glm::vec3& normal)
    {
        const unsigned int first = static_cast<unsigned int>(mesh.vertices.size() / 11);
        AddVertex(mesh, center - u * 0.5f - v * 0.5f, normal, { 0.0f, 0.0f });
        AddVertex(mesh, center + u * 0.5f - v * 0.5f, normal, { 1.0f, 0.0f });
        AddVertex(mesh, center + u * 0.5f + v * 0.5f, normal, { 1.0f, 1.0f });
        AddVertex(mesh, center - u * 0.5f + v * 0.5f, normal, { 0.0f, 1.0f });
        mesh.indices.insert(mesh.indices.end(), { first, first + 1, first + 2, first, first + 2, first + 3 });
    }

    // Tronco de cono de altura 1 centrado en el origen. Cilindro: radios iguales; cono: rTop = 0.
    // Las tapas solo se crean para un radio mayor que 0.
    Mesh CreateTruncatedCone(float rTop, float rBottom, unsigned int segments)
    {
        Mesh mesh;
        const float height = 1.0f;
        const float twoPi = glm::two_pi<float>();

        // Lateral: dos vertices por segmento (arriba y abajo), con la normal inclinada segun la
        // pendiente; en el cilindro (rBottom - rTop = 0) es horizontal.
        for (unsigned int j = 0; j <= segments; ++j)
        {
            const float t = static_cast<float>(j) / segments;
            const float theta = t * twoPi;
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            const glm::vec3 n = glm::normalize(glm::vec3(height * c, rBottom - rTop, height * s));
            AddVertex(mesh, { rTop * c, 0.5f * height, rTop * s }, n, { t, 1.0f });
            AddVertex(mesh, { rBottom * c, -0.5f * height, rBottom * s }, n, { t, 0.0f });
        }
        for (unsigned int j = 0; j < segments; ++j)
        {
            const unsigned int top = 2 * j, bottom = 2 * j + 1, nextTop = 2 * j + 2, nextBottom = 2 * j + 3;
            mesh.indices.insert(mesh.indices.end(), { top, nextTop, bottom, nextTop, nextBottom, bottom });
        }

        // Tapas: abanico de triangulos con vertices propios para que su normal sea plana.
        auto addCap = [&](float radius, float y, float normalY)
            {
                const unsigned int center = static_cast<unsigned int>(mesh.vertices.size() / 11);
                const glm::vec3 n(0.0f, normalY, 0.0f);
                AddVertex(mesh, { 0.0f, y, 0.0f }, n, { 0.5f, 0.5f });
                for (unsigned int j = 0; j <= segments; ++j)
                {
                    const float theta = static_cast<float>(j) / segments * twoPi;
                    const float c = std::cos(theta);
                    const float s = std::sin(theta);
                    AddVertex(mesh, { radius * c, y, radius * s }, n, { 0.5f + 0.5f * c, 0.5f + 0.5f * s });
                }
                for (unsigned int j = 0; j < segments; ++j)
                {
                    const unsigned int a = center + 1 + j, b = center + 2 + j;
                    if (normalY > 0.0f) mesh.indices.insert(mesh.indices.end(), { center, b, a });
                    else mesh.indices.insert(mesh.indices.end(), { center, a, b });
                }
            };
        if (rTop > 0.0f) addCap(rTop, 0.5f * height, 1.0f);
        if (rBottom > 0.0f) addCap(rBottom, -0.5f * height, -1.0f);

        mesh.SetupMesh();
        return mesh;
    }
}

Mesh CreateCubeMesh()
{
    Mesh mesh;
    // Una cara por normal; (u, v) cumple u x v = normal.
    struct Face { glm::vec3 n, u, v; };
    const Face faces[6] = {
        { { 1, 0, 0 },  { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1, 0, 0 }, { 0, 0, 1 },  { 0, 1, 0 } },
        { { 0, 1, 0 },  { 1, 0, 0 },  { 0, 0, -1 } },
        { { 0, -1, 0 }, { 1, 0, 0 },  { 0, 0, 1 } },
        { { 0, 0, 1 },  { 1, 0, 0 },  { 0, 1, 0 } },
        { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    };
    for (const Face& f : faces)
        AddQuad(mesh, f.n * 0.5f, f.u, f.v, f.n);
    mesh.SetupMesh();
    return mesh;
}

Mesh CreatePlaneMesh()
{
    Mesh mesh;
    AddQuad(mesh, glm::vec3(0.0f), { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });
    mesh.SetupMesh();
    return mesh;
}

Mesh CreateSphereMesh(unsigned int sectors, unsigned int stacks)
{
    Mesh mesh;
    const float radius = 0.5f;
    for (unsigned int i = 0; i <= stacks; ++i)
    {
        const float phi = glm::pi<float>() * i / stacks;   // 0 = polo superior
        for (unsigned int j = 0; j <= sectors; ++j)
        {
            const float theta = glm::two_pi<float>() * j / sectors;
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            AddVertex(mesh, n * radius, n, { static_cast<float>(j) / sectors, 1.0f - static_cast<float>(i) / stacks });
        }
    }
    for (unsigned int i = 0; i < stacks; ++i)
    {
        for (unsigned int j = 0; j < sectors; ++j)
        {
            const unsigned int a = i * (sectors + 1) + j, b = a + sectors + 1;
            mesh.indices.insert(mesh.indices.end(), { a, a + 1, b, a + 1, b + 1, b });
        }
    }
    mesh.SetupMesh();
    return mesh;
}

Mesh CreateCylinderMesh(unsigned int segments)
{
    return CreateTruncatedCone(0.5f, 0.5f, segments);
}

Mesh CreateConeMesh(unsigned int segments)
{
    return CreateTruncatedCone(0.0f, 0.5f, segments);
}
