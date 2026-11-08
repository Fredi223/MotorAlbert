#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include "Logger.h"
#include "Scene.h"
#include "SceneSerializer.h"

void Simulation::Play(const Scene& scene)
{
    if (state == State::Paused)
    {
        state = State::Playing;
        Console::LogInfo("Play: continua (t = %.2f s)", gameTime);
        return;
    }
    if (state != State::Stopped) return;

    savedScene = SceneSerializer::ToJson(scene).dump();
    gameTime = 0.0;
    gameFrames = 0;
    stepRequested = false;
    state = State::Playing;
    Console::LogInfo("Play: escena guardada en memoria (%zu bytes)", savedScene.size());
}

void Simulation::Pause()
{
    if (state != State::Playing) return;
    state = State::Paused;
    Console::LogInfo("Pause (t = %.2f s)", gameTime);
}

bool Simulation::Stop(Scene& scene, ResourceManager& resources)
{
    if (state == State::Stopped) return true;
    const nlohmann::json saved = nlohmann::json::parse(savedScene, nullptr, false);
    if (saved.is_discarded() || !SceneSerializer::FromJson(saved, scene, resources))
    {
        Console::LogError("Stop: no se pudo restaurar la escena guardada al pulsar Play");
        return false;
    }
    state = State::Stopped;
    savedScene.clear();
    Console::LogInfo("Stop: escena restaurada (%.2f s y %llu frames de juego)", gameTime, (unsigned long long)gameFrames);
    return true;
}

// Logica de juego de los componentes. De momento solo el Rotator.
static bool UpdateComponents(Scene& scene, float dt)
{
    bool moved = false;
    scene.ForEach([dt, &moved](GameObject& go) {
        const auto* rotator = go.components.Get<ComponentRotator>();
        if (!rotator || !rotator->enabled || rotator->degreesPerSecond == glm::vec3(0.0f)) return;
        // remainder deja el angulo en [-180, 180]: no crece sin limite en un Play largo.
        for (int axis = 0; axis < 3; ++axis)
            go.transform.rotation[axis] = std::remainder(go.transform.rotation[axis] + rotator->degreesPerSecond[axis] * dt, 360.0f);
        moved = true;
    });
    return moved;
}

float Simulation::Tick(Scene& scene, float realDeltaSeconds)
{
    float dt = 0.0f;
    if (state == State::Playing)
        dt = std::min(realDeltaSeconds, MAX_DELTA_SECONDS) * timeScale;
    else if (state == State::Paused && stepRequested)
        dt = STEP_SECONDS * timeScale;
    stepRequested = false;
    if (dt <= 0.0f) return 0.0f;

    gameTime += dt;
    ++gameFrames;
    // Los objetos que se mueven cambian su AABB de mundo: el octree se reconstruye ya (antes del
    // render de este frame) para que culling y picking los encuentren donde estan.
    if (UpdateComponents(scene, dt)) scene.RebuildOctree();
    return dt;
}
