#include "FrameTiming.h"

#include <cmath>

#include "Logger.h"

void FrameTiming::Init()
{
    lastFrameTick = SDL_GetPerformanceCounter();
    frameTimes.assign(HISTORY, 0.0f);
}

void FrameTiming::ApplyVsync() const
{
    if (!SDL_GL_SetSwapInterval(vsync ? 1 : 0))
        Console::LogError("Error configurando Swap Interval: %s", SDL_GetError());
}

void FrameTiming::EndFrame()
{
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    float deltaMs = (float)((now - lastFrameTick) * 1000.0 / (double)freq);
    lastFrameTick = now;

    frameTimes.push_back(deltaMs);
    if (frameTimes.size() > HISTORY) frameTimes.pop_front();

    if (fpsCap > 0) {
        float targetMs = 1000.0f / (float)fpsCap;
        if (deltaMs < targetMs) {
            Uint32 waitMs = (Uint32)std::ceil(targetMs - deltaMs);
            SDL_Delay(waitMs);
            Uint64 now2 = SDL_GetPerformanceCounter();
            deltaMs += (float)((now2 - lastFrameTick) * 1000.0 / (double)freq);
            lastFrameTick = now2;
            frameTimes.back() = deltaMs;
        }
    }
}
