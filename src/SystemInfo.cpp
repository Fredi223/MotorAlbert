#include "SystemInfo.h"

// Se aisla en su propio .cpp porque windows.h define macros (min, max, APIENTRY) que chocan con
// GLAD, GLM y el resto del motor.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

bool SystemInfo::GetProcessMemory(size_t& workingSet, size_t& privateBytes)
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return false;
    workingSet = pmc.WorkingSetSize;
    privateBytes = pmc.PrivateUsage;
    return true;
}
#else
bool SystemInfo::GetProcessMemory(size_t&, size_t&) { return false; }
#endif
