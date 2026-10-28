#pragma once
#include <cstddef>

namespace SystemInfo
{
    // Memoria del proceso en bytes. Devuelve false si el sistema no la facilita.
    // workingSet: memoria fisica en uso ahora; privateBytes: memoria que el proceso ha reservado.
    bool GetProcessMemory(size_t& workingSet, size_t& privateBytes);
}
