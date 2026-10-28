#pragma once
#include <glm/glm.hpp>

// Camara del editor: libre (mirar + mover) y orbital alrededor de un target. Solo guarda el estado
// y aplica los movimientos; leer el raton y el teclado es cosa de quien la usa.
struct Camera
{
    static constexpr float FOV_DEG = 60.0f;                   // FOV vertical; lo usan la proyeccion, el encuadre (F) y el picking
    static constexpr float NEAR_PLANE = 0.1f;
    static constexpr float FAR_PLANE = 100.0f;
    static constexpr float SPRINT_MULTIPLIER = 2.0f;          // factor al mantener Shift (la guia de entrega pide duplicar)
    static constexpr float MAX_DELTA_TIME = 0.1f;             // s; acota tirones para no teletransportar la camara
    static constexpr float ORBIT_MIN_RADIUS_FACTOR = 0.75f;   // distancia minima = factor * radio del objeto...
    static constexpr float ORBIT_MIN_DISTANCE_FLOOR = 0.25f;  // ...con este suelo si no hay objeto
    static constexpr float ORBIT_MAX_DISTANCE = 90.0f;        // el far plane esta en 100
    static constexpr float FRAME_MARGIN = 1.2f;               // holgura alrededor del objeto al encuadrar

    glm::vec3 position = glm::vec3(0.0f, 2.0f, 5.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f;
    float pitch = 0.0f;

    float moveSpeed = 3.0f;          // unidades de mundo por segundo
    float mouseSensitivity = 0.1f;   // grados por pixel
    float zoomStep = 0.1f;           // fraccion de orbitDistance por muesca de rueda

    // Orbital: la camara esta en orbitTarget - front * orbitDistance.
    glm::vec3 orbitTarget = glm::vec3(0.0f);
    float orbitDistance = 5.0f;      // distancia camara-target
    float orbitRadius = 0.0f;        // semidiagonal del AABB del target (0 = origen)

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix(float aspect) const;

    // Aplica un delta de raton (pixeles) a yaw/pitch.
    void Rotate(float dx, float dy);

    // Camara libre. Los ejes van de -1 a 1 (adelante, derecha, arriba) en el espacio de la camara.
    void Move(float forwardAxis, float rightAxis, float upAxis, float deltaTime, bool sprint);

    // Empieza a orbitar alrededor de orbitTarget: mira hacia el sin cambiar de posicion.
    void BeginOrbit();
    // Gira alrededor de orbitTarget con un delta de raton.
    void Orbit(float dx, float dy);

    // Rueda: en modo orbital acerca/aleja sobre la esfera; si no, dolly a lo largo de front
    // tomando la distancia real a orbitTarget como referencia.
    void Zoom(float wheelDelta, bool orbiting);

    // Coloca la camara para que orbitTarget/orbitRadius quepan en el viewport, sin girar la vista.
    void FrameTarget(float aspect);

private:
    void UpdateFront();
    void AimAt(const glm::vec3& target);
    float OrbitMinDistance() const;
};
