#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace Console
{
    // Inicializar (opcional)
    void Init();

    // Añadir líneas al log
    void LogInfo(const char* fmt, ...);
    void LogError(const char* fmt, ...);

    // Borrar el log
    void Clear();

    // Dibuja la ventana ImGui del Console (llamar desde Main.cpp donde haces ImGui::Begin("Console"))
    // title = "Console", optional boolean pointer para mostrar/ocultar
    void DrawConsole(const char* title, bool* p_open = nullptr);
}
