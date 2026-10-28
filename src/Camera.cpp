#include "Camera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::GetProjectionMatrix(float aspect) const
{
    return glm::perspective(glm::radians(FOV_DEG), aspect, NEAR_PLANE, FAR_PLANE);
}

// Recalcula front a partir de yaw/pitch (compartido por libre y orbital).
void Camera::UpdateFront()
{
    glm::vec3 f;
    f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f.y = sin(glm::radians(pitch));
    f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(f);
}

// Inversa de UpdateFront: orienta yaw/pitch para mirar desde position hacia target.
void Camera::AimAt(const glm::vec3& target)
{
    glm::vec3 d = glm::normalize(target - position);
    yaw = glm::degrees(atan2(d.z, d.x));
    pitch = glm::clamp(glm::degrees(asin(d.y)), -89.0f, 89.0f);
    UpdateFront();
}

float Camera::OrbitMinDistance() const
{
    return std::max(ORBIT_MIN_DISTANCE_FLOOR, orbitRadius * ORBIT_MIN_RADIUS_FACTOR);
}

void Camera::Rotate(float dx, float dy)
{
    yaw += dx * mouseSensitivity;
    pitch += -dy * mouseSensitivity;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    UpdateFront();
}

void Camera::Move(float forwardAxis, float rightAxis, float upAxis, float deltaTime, bool sprint)
{
    deltaTime = std::min(deltaTime, MAX_DELTA_TIME);
    float speed = moveSpeed * deltaTime;
    if (sprint) speed *= SPRINT_MULTIPLIER;

    // Se suma la direccion de cada eje y se normaliza el conjunto, para que combinar teclas
    // (p. ej. W+D) no sea x raiz de 2 mas rapido que ir en linea recta. Ejes opuestos se anulan.
    glm::vec3 right = glm::normalize(glm::cross(front, up));
    glm::vec3 moveDir = front * forwardAxis + right * rightAxis + up * upAxis;
    if (glm::length(moveDir) > 1e-6f) position += glm::normalize(moveDir) * speed;
}

// Se orienta la camara hacia el target SIN moverla, asi position ya cumple
// position = target - front * distance y no hay salto de posicion.
void Camera::BeginOrbit()
{
    orbitDistance = glm::length(position - orbitTarget);
    if (orbitDistance > 1e-4f) AimAt(orbitTarget);
    orbitDistance = glm::clamp(orbitDistance, OrbitMinDistance(), ORBIT_MAX_DISTANCE);
    position = orbitTarget - front * orbitDistance;
}

void Camera::Orbit(float dx, float dy)
{
    Rotate(dx, dy);
    position = orbitTarget - front * orbitDistance;
}

void Camera::Zoom(float wheelDelta, bool orbiting)
{
    if (!orbiting) orbitDistance = glm::length(position - orbitTarget);

    float oldDistance = orbitDistance;
    float newDistance = oldDistance * std::pow(1.0f - zoomStep, wheelDelta);
    // Sin atravesar el objeto ni salir del far plane, pero sin atrapar a una
    // camara que ya esta fuera de ese rango: solo se limita el sentido que empeora.
    if (newDistance < oldDistance)
        newDistance = std::max(newDistance, std::min(oldDistance, OrbitMinDistance()));
    else
        newDistance = std::min(newDistance, std::max(oldDistance, ORBIT_MAX_DISTANCE));
    orbitDistance = newDistance;

    if (orbiting) position = orbitTarget - front * orbitDistance;
    else position += front * (oldDistance - newDistance);
}

void Camera::FrameTarget(float aspect)
{
    // La esfera que envuelve el AABB debe caber en el FOV mas estrecho
    // (vertical u horizontal, segun el aspecto del viewport).
    float halfFovY = glm::radians(FOV_DEG) * 0.5f;
    float halfFovX = atan(tan(halfFovY) * aspect);
    float halfFov = std::min(halfFovY, halfFovX);

    float radius = std::max(orbitRadius, 0.01f);
    orbitDistance = radius / sin(halfFov) * FRAME_MARGIN;
    orbitDistance = glm::clamp(orbitDistance, OrbitMinDistance(), ORBIT_MAX_DISTANCE);
    position = orbitTarget - front * orbitDistance;
}
