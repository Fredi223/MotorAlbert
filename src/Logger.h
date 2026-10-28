#pragma once

// Log del motor: cada linea se guarda para la ventana Console y se copia en la terminal.
namespace Console
{
    void Init();

    void LogInfo(const char* fmt, ...);
    void LogError(const char* fmt, ...);   // con prefijo [ERR], se pinta en rojo

    void Clear();

    void DrawConsole(const char* title, bool* p_open = nullptr);
}
