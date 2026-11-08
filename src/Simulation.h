#pragma once
#include <cstdint>
#include <string>

class ResourceManager;
class Scene;

// Play / Pause / Stop de la escena.
//
// Play guarda la escena en memoria (el mismo JSON que un .scene, SceneSerializer) y empieza a correr
// el tiempo de juego; Stop restaura ese estado inicial, asi que todo lo que pase durante el Play
// (objetos girados, movidos o borrados) se deshace.
//
// El tiempo de juego va aparte del real: solo avanza en Play, se multiplica por timeScale y se para
// en Pause. Step avanza un unico frame estando en pausa.
class Simulation
{
public:
    enum class State { Stopped, Playing, Paused };

    State GetState() const { return state; }
    bool IsStopped() const { return state == State::Stopped; }

    // Parado: guarda la escena y empieza. En pausa: continua.
    void Play(const Scene& scene);
    void Pause();
    // Restaura la escena guardada al pulsar Play. false si no se pudo (la escena se queda como esta).
    bool Stop(Scene& scene, ResourceManager& resources);
    // En pausa: el siguiente Tick avanza un frame (de STEP_SECONDS).
    void Step() { stepRequested = true; }

    // Llamar una vez por frame con el tiempo real del frame. Avanza el tiempo de juego y actualiza la
    // escena si toca. Devuelve el delta de juego aplicado (0 si no ha avanzado).
    float Tick(Scene& scene, float realDeltaSeconds);

    double GameTime() const { return gameTime; }
    uint64_t GameFrames() const { return gameFrames; }

    float timeScale = 1.0f;

    static constexpr float STEP_SECONDS = 1.0f / 60.0f;
    static constexpr float MAX_DELTA_SECONDS = 0.1f;   // un frame muy largo (cargar algo) no da un salto enorme

private:
    State state = State::Stopped;
    std::string savedScene;   // JSON de la escena al pulsar Play
    double gameTime = 0.0;
    uint64_t gameFrames = 0;
    bool stepRequested = false;
};
