#include <string>

#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <assimp/version.h>

#include "imgui.h"

#include "Editor.h"
#include "FrameTiming.h"
#include "Logger.h"
#include "Renderer.h"
#include "Scene.h"
#include "UndoHistory.h"

// ============================================================
// INICIALIZACIÓN DE SDL + OpenGL
// ============================================================
bool InitOpenGL(SDL_Window** window, SDL_GLContext* context, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        Console::LogError("Error inicializando SDL3: %s", SDL_GetError());
        return false;
    }
    Console::LogInfo("SDL3 inicializado correctamente.");


    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    *window = SDL_CreateWindow(
        "Motor Albert",
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!*window)
    {
        Console::LogError("Error creando ventana SDL3: %s", SDL_GetError());
        return false;
    }
    Console::LogInfo("Ventana creada correctamente (%dx%d).", width, height);

    *context = SDL_GL_CreateContext(*window);
    if (!*context)
    {
        Console::LogError("Error creando contexto OpenGL: %s", SDL_GetError());
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        Console::LogError("Error cargando funciones GLAD: %s", SDL_GetError());
        return false;
    }

    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);

    Console::LogInfo("OpenGL %s inicializado correctamente (GLAD).", (const char*)glGetString(GL_VERSION));
    Console::LogInfo("GPU: %s", (const char*)glGetString(GL_RENDERER));

    return true;
}

// ============================================================
// PROGRAMA PRINCIPAL
// ============================================================
// Motor (Renderer, Scene, FrameTiming) y editor (Editor, UndoHistory) se crean aqui y se conectan;
// el bucle solo reparte los eventos al editor, le pide un frame y mide el tiempo.
int main(int argc, char* argv[])
{
    Console::Init();

    SDL_Window* window = nullptr;
    SDL_GLContext context;

    if (!InitOpenGL(&window, &context, 1280, 720))
    {
        Console::LogError("No se pudo inicializar OpenGL/SDL: %s", SDL_GetError());
        return -1;
    }

    Console::LogInfo("Inicializando motor - version E1");
    Console::LogInfo("Librerias: SDL %d.%d.%d, Assimp %u.%u.%u, ImGui %s, stb_image (texturas), GLM, GLAD",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
        aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionRevision(), IMGUI_VERSION);

    FrameTiming timing;
    timing.Init();
    timing.ApplyVsync();

    Renderer renderer;
    renderer.Init(1280, 720);

    // La escena empieza vacia; RebuildOctree recalcula los limites del octree cada vez que se
    // anade algo (la Baker House inicial, drag & drop o una primitiva).
    Scene scene;
    scene.defaultTexture = renderer.GetCheckerTexture();

    // El historial de undo guarda snapshots que pueden usar texturas que ya no estan en la
    // escena: la escena le pregunta antes de liberar una.
    UndoHistory history;
    scene.isTextureUsedElsewhere = [&history](GLuint tex) { return history.UsesTexture(tex); };

    // Baker House se carga al arrancar desde Resources/, que vive junto al ejecutable (CMake
    // la copia ahi al compilar): asi funciona igual desde build/ que desde la carpeta de entrega.
    {
        const char* basePath = SDL_GetBasePath();
        const std::string bakerHouse = std::string(basePath ? basePath : "") + "Resources/BakerHouse.fbx";
        scene.LoadFbx(bakerHouse);
    }

    Editor editor(window, scene, history, renderer, timing);
    editor.Init(context);

    Console::LogInfo("Iniciando bucle principal");

    // ========================================================
    // BUCLE PRINCIPAL
    // ========================================================
    while (!editor.WantsToQuit())
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            editor.ProcessEvent(e);

        editor.DrawFrame();

        timing.EndFrame();
        SDL_GL_SwapWindow(window);
    }

    // ========================================================
    // LIMPIEZA
    // ========================================================
    Console::LogInfo("Saliendo del bucle principal");

    for (auto& go : scene.objects) go.Destroy();
    scene.objects.clear();

    editor.Shutdown();
    renderer.Shutdown();

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
