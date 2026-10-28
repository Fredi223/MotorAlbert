#pragma once
#include <deque>
#include <SDL3/SDL.h>

// Medida del tiempo de cada frame, limite de FPS y vsync.
class FrameTiming
{
public:
    static constexpr size_t HISTORY = 200;   // frames que se guardan para la grafica de Configuration

    int fpsCap = 60;    // 0 = sin limite
    bool vsync = true;

    // Llamar una vez antes del bucle principal.
    void Init();

    // Llamar al final de cada frame (antes del swap): mide lo que ha durado, lo guarda en el
    // historial y, si va mas rapido que fpsCap, espera lo que falta.
    void EndFrame();

    // Aplica vsync al contexto de OpenGL actual.
    void ApplyVsync() const;

    // Duracion del ultimo frame completo, en segundos.
    float LastFrameSeconds() const { return frameTimes.empty() ? 0.0f : frameTimes.back() / 1000.0f; }
    const std::deque<float>& FrameTimesMs() const { return frameTimes; }

private:
    std::deque<float> frameTimes;   // ms
    Uint64 lastFrameTick = 0;
};
