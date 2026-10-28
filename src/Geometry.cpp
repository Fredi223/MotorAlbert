#include "Geometry.h"

#include <cfloat>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>

bool RayIntersectsAABB(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& aabbMin, const glm::vec3& aabbMax,
    float& outT)
{
    float tmin = (aabbMin.x - rayOrig.x) / (rayDir.x == 0.0f ? 1e-8f : rayDir.x);
    float tmax = (aabbMax.x - rayOrig.x) / (rayDir.x == 0.0f ? 1e-8f : rayDir.x);
    if (tmin > tmax) std::swap(tmin, tmax);

    float tymin = (aabbMin.y - rayOrig.y) / (rayDir.y == 0.0f ? 1e-8f : rayDir.y);
    float tymax = (aabbMax.y - rayOrig.y) / (rayDir.y == 0.0f ? 1e-8f : rayDir.y);
    if (tymin > tymax) std::swap(tymin, tymax);

    if ((tmin > tymax) || (tymin > tmax)) return false;

    if (tymin > tmin) tmin = tymin;
    if (tymax < tmax) tmax = tymax;

    float tzmin = (aabbMin.z - rayOrig.z) / (rayDir.z == 0.0f ? 1e-8f : rayDir.z);
    float tzmax = (aabbMax.z - rayOrig.z) / (rayDir.z == 0.0f ? 1e-8f : rayDir.z);
    if (tzmin > tzmax) std::swap(tzmin, tzmax);

    if ((tmin > tzmax) || (tzmin > tmax)) return false;

    if (tzmin > tmin) tmin = tzmin;
    if (tzmax < tmax) tmax = tzmax;
    if (tmax < 0.0f) return false;

    outT = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}


// Planos del frustum a partir de la matriz view-projection (metodo de Gribb/Hartmann).
// Con clip = vp * (x,y,z,1), un punto esta dentro si -w <= x,y,z <= w; cada desigualdad es un
// plano, p. ej. w + x >= 0 (izquierdo) = (fila3 + fila0) . (x,y,z,1) >= 0. Los planos salen de
// las FILAS de vp. GLM guarda las matrices por columnas (vp[i] es la columna i), asi que se
// transpone para que rows[i] sea la fila i. Profundidad de OpenGL por defecto (-w <= z <= w).
Frustum ExtractFrustum(const glm::mat4& vp)
{
    Frustum f;
    const glm::mat4 rows = glm::transpose(vp);
    f.planes[0] = rows[3] + rows[0]; // izquierdo
    f.planes[1] = rows[3] - rows[0]; // derecho
    f.planes[2] = rows[3] + rows[1]; // inferior
    f.planes[3] = rows[3] - rows[1]; // superior
    f.planes[4] = rows[3] + rows[2]; // cercano
    f.planes[5] = rows[3] - rows[2]; // lejano
    for (int i = 0; i < 6; ++i)
    {
        float len = glm::length(glm::vec3(f.planes[i]));
        f.planes[i] /= len;
    }

    return f;
}

bool AABBInsideFrustum(const Frustum& f,
    const glm::vec3& minB,
    const glm::vec3& maxB)
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec4& p = f.planes[i];

        glm::vec3 positive = minB;
        if (p.x >= 0) positive.x = maxB.x;
        if (p.y >= 0) positive.y = maxB.y;
        if (p.z >= 0) positive.z = maxB.z;

        if (glm::dot(glm::vec3(p), positive) + p.w < 0)
            return false;
    }
    return true;
}

void ComputeSceneAABB(const std::vector<GameObject>& objects,
    glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);

    for (const auto& go : objects)
    {
        glm::mat4 m = go.transform.GetMatrix();

        glm::vec3 corners[8] = {
            {go.aabbMin.x, go.aabbMin.y, go.aabbMin.z},
            {go.aabbMax.x, go.aabbMin.y, go.aabbMin.z},
            {go.aabbMin.x, go.aabbMax.y, go.aabbMin.z},
            {go.aabbMax.x, go.aabbMax.y, go.aabbMin.z},
            {go.aabbMin.x, go.aabbMin.y, go.aabbMax.z},
            {go.aabbMax.x, go.aabbMin.y, go.aabbMax.z},
            {go.aabbMin.x, go.aabbMax.y, go.aabbMax.z},
            {go.aabbMax.x, go.aabbMax.y, go.aabbMax.z}
        };

        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 w = glm::vec3(m * glm::vec4(corners[i], 1.0f));
            outMin = glm::min(outMin, w);
            outMax = glm::max(outMax, w);
        }
    }
    glm::vec3 size = outMax - outMin;
    outMin -= size * 0.1f;
    outMax += size * 0.1f;
}

// ============================================================
// CONVERTIR POSICIÓN DE RATÓN A RAYO EN ESPACIO MUNDIAL
// ============================================================
// Direccion del rayo que sale de la camara por el pixel (mouseX, mouseY) del viewport.
// mouseX/mouseY son PIXELES RELATIVOS a la esquina superior izquierda del viewport (Y hacia abajo,
// como los da ImGui); viewportW/H es su tamano en pixeles. glm::unProject espera coordenadas de
// ventana con el origen ABAJO a la izquierda y las compara con el viewport que se le pasa, asi que:
// el viewport es (0, 0, W, H) (el mouse ya es relativo; sumarle la posicion absoluta del viewport en
// pantalla desplazaba el rayo) y la Y se invierte UNA sola vez (H - mouseY).
glm::vec3 ScreenToWorldRay_UnProject(float mouseX, float mouseY,
    float viewportW, float viewportH,
    const glm::mat4& projection, const glm::mat4& view)
{
    const glm::vec4 viewport(0.0f, 0.0f, viewportW, viewportH);
    const float winY = viewportH - mouseY;
    glm::vec3 winNear = glm::unProject(glm::vec3(mouseX, winY, 0.0f), view, projection, viewport);
    glm::vec3 winFar = glm::unProject(glm::vec3(mouseX, winY, 1.0f), view, projection, viewport);
    return glm::normalize(winFar - winNear);
}

// AABB local de la malla. Todas las mallas usan el formato de vertice de SetupMesh:
// 11 floats (pos3, normal3, uv2, color3), con la posicion al principio.
bool ComputeAABBFromMesh(const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax)
{
    const size_t VERTEX_STRIDE_FLOATS = 11;
    outMin = glm::vec3(0.0f);
    outMax = glm::vec3(0.0f);
    if (mesh.vertices.size() < VERTEX_STRIDE_FLOATS) return false;

    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (size_t idx = 0; idx + 2 < mesh.vertices.size(); idx += VERTEX_STRIDE_FLOATS)
    {
        glm::vec3 pos(mesh.vertices[idx + 0], mesh.vertices[idx + 1], mesh.vertices[idx + 2]);
        minB = glm::min(minB, pos);
        maxB = glm::max(maxB, pos);
    }

    outMin = minB;
    outMax = maxB;
    return true;
}
