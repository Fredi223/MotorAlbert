#include "Geometry.h"

#include <cfloat>
#include <cmath>
#include <algorithm>
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

    outT = std::max(tmin, 0.0f);   // distancia de entrada; 0 si el origen ya esta dentro de la caja
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

glm::mat4 CameraViewFromWorld(const glm::mat4& world)
{
    const glm::vec3 position(world[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
    const glm::vec3 up = glm::normalize(glm::vec3(world[1]));
    return glm::lookAt(position, position + forward, up);
}

void FrustumCorners(const glm::mat4& vp, glm::vec3 outCorners[8])
{
    // Las esquinas del cubo de NDC (-1..1 en x, y, z) llevadas a mundo con la inversa.
    const glm::mat4 inv = glm::inverse(vp);
    static const glm::vec2 xy[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec4 p = inv * glm::vec4(xy[i % 4], i < 4 ? -1.0f : 1.0f, 1.0f);
        outCorners[i] = glm::vec3(p) / p.w;
    }
}

bool RayIntersectsTriangle(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& outT)
{
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 p = glm::cross(rayDir, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < 1e-12f) return false;   // rayo paralelo al triangulo

    const float invDet = 1.0f / det;
    const glm::vec3 s = rayOrig - v0;
    const float u = glm::dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(rayDir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = glm::dot(e2, q) * invDet;
    if (t <= 0.0f) return false;
    outT = t;
    return true;
}

bool RayIntersectsMesh(const Mesh& mesh, const glm::vec3& rayOrig, const glm::vec3& rayDir, float& outT)
{
    const size_t VERTEX_STRIDE_FLOATS = 11;   // el formato de SetupMesh (ver ComputeAABBFromMesh)
    const size_t vertexCount = mesh.vertices.size() / VERTEX_STRIDE_FLOATS;
    auto pos = [&](unsigned int i) {
        const float* v = &mesh.vertices[size_t(i) * VERTEX_STRIDE_FLOATS];
        return glm::vec3(v[0], v[1], v[2]);
    };

    bool hit = false;
    float best = FLT_MAX;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const unsigned int a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount) continue;
        float t = 0.0f;
        if (RayIntersectsTriangle(rayOrig, rayDir, pos(a), pos(b), pos(c), t) && t < best)
        {
            best = t;
            hit = true;
        }
    }
    if (hit) outT = best;
    return hit;
}
