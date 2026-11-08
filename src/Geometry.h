#pragma once
#include <glm/glm.hpp>

#include "ModuleModelLoader.h"

// Calculos geometricos del motor: AABB, frustum culling y raycast para el picking.

struct Frustum
{
    glm::vec4 planes[6];   // normalizados; un punto p esta dentro si dot(xyz, p) + w >= 0 en los 6
};

// Planos del frustum a partir de la matriz view-projection.
Frustum ExtractFrustum(const glm::mat4& vp);

// true si el AABB (en coordenadas de mundo) toca o esta dentro del frustum.
bool AABBInsideFrustum(const Frustum& f, const glm::vec3& minB, const glm::vec3& maxB);

// Interseccion rayo-AABB (metodo de slabs). outT es la distancia a la que el rayo entra en la caja
// (0 si el origen ya esta dentro).
bool RayIntersectsAABB(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& aabbMin, const glm::vec3& aabbMax,
    float& outT);

// Direccion (normalizada) del rayo que sale de la camara por un pixel del viewport.
glm::vec3 ScreenToWorldRay_UnProject(float mouseX, float mouseY,
    float viewportW, float viewportH,
    const glm::mat4& projection, const glm::mat4& view);

// AABB local de una malla. Devuelve false si la malla no tiene vertices.
bool ComputeAABBFromMesh(const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax);

// Matriz de vista de una camara de juego a partir de la matriz de mundo de su GameObject: mira
// hacia su -Z local con +Y arriba. Se ignora la escala (una camara escalada no deforma la vista).
glm::mat4 CameraViewFromWorld(const glm::mat4& world);

// Las 8 esquinas (en mundo) del frustum de una matriz view-projection: primero las 4 del plano
// cercano y luego las 4 del lejano, en el orden (-x,-y) (+x,-y) (+x,+y) (-x,+y).
void FrustumCorners(const glm::mat4& vp, glm::vec3 outCorners[8]);

// Interseccion rayo-triangulo (Moller-Trumbore, por las dos caras). outT en unidades de rayDir.
bool RayIntersectsTriangle(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& outT);

// Primer impacto del rayo con los triangulos de la malla. El rayo va en el espacio LOCAL de la
// malla y outT en unidades de rayDir: si el rayo local es inversa(mundo) * rayo de mundo, outT es
// tambien la distancia en el rayo de mundo.
bool RayIntersectsMesh(const Mesh& mesh, const glm::vec3& rayOrig, const glm::vec3& rayDir, float& outT);
