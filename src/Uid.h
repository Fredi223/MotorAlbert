#pragma once
#include <cstdint>
#include <random>

// Identificadores de 64 bits de GameObjects y recursos: es lo que se guarda en los archivos (nunca
// punteros). 0 = "ninguno" (y la raiz de la escena). Los valores por debajo de UID_FIRST_RANDOM estan
// reservados para los recursos internos del motor (las primitivas), que tienen UID fijos.
constexpr uint64_t UID_FIRST_RANDOM = 1024;

inline uint64_t GenerateUID()
{
    static std::mt19937_64 rng{ std::random_device{}() };
    uint64_t uid = 0;
    while (uid < UID_FIRST_RANDOM) uid = rng();
    return uid;
}
