#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "GameObject.h"

// Calculos geometricos del motor: AABB, frustum culling y raycast para el picking.

struct Frustum
{
    glm::vec4 planes[6];   // normalizados; un punto p esta dentro si dot(xyz, p) + w >= 0 en los 6
};

// Planos del frustum a partir de la matriz view-projection.
Frustum ExtractFrustum(const glm::mat4& vp);

// true si el AABB (en coordenadas de mundo) toca o esta dentro del frustum.
bool AABBInsideFrustum(const Frustum& f, const glm::vec3& minB, const glm::vec3& maxB);

// Interseccion rayo-AABB (metodo de slabs). outT es la distancia al primer impacto.
bool RayIntersectsAABB(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& aabbMin, const glm::vec3& aabbMax,
    float& outT);

// Direccion (normalizada) del rayo que sale de la camara por un pixel del viewport.
glm::vec3 ScreenToWorldRay_UnProject(float mouseX, float mouseY,
    float viewportW, float viewportH,
    const glm::mat4& projection, const glm::mat4& view);

// AABB de mundo que engloba todos los objetos, con un 10% de holgura (limites del octree).
void ComputeSceneAABB(const std::vector<GameObject>& objects, glm::vec3& outMin, glm::vec3& outMax);

// AABB local de una malla. Devuelve false si la malla no tiene vertices.
bool ComputeAABBFromMesh(const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax);
