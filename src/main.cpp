#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <limits>

#include <cfloat>
#include "ModuleModelLoader.h"
#include "GameObject.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Octree.h"

#include "Logger.h"

#include <deque>
#include <assimp/version.h> 

int g_fpsCap = 60;                      
ImVec4 g_clearColor = ImVec4(0.1f, 0.1f, 0.1f, 1.0f); 
bool g_showPerfGraph = true;

bool g_enableFrustumCulling = true;

bool g_vsync = true;   // <-- AÑADIR ESTA LÍNEA

static const int PERF_HISTORY = 200;
std::deque<float> g_frameTimes; // ms
Uint64 g_lastFrameTick = 0;


GLuint checkerTexture = 0;
// ============================================================
// FUNCIONES AUXILIARES
// ============================================================
inline float compMax(const glm::vec3& v) {
    return std::max(v.x, std::max(v.y, v.z));
}


// ============================================================
// SHADERS BÁSICOS
// ============================================================
const char* vertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;   // <--- NUEVO
layout (location = 3) in vec3 aColor;

out vec3 Normal;
out vec3 FragPos;
out vec3 Color;
out vec2 TexCoord;                         // <--- NUEVO

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    FragPos = vec3(uModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    Color = aColor;
    TexCoord = aTexCoord;                  // <--- PASAR UV
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 460 core
in vec3 Normal;
in vec3 Color;
in vec2 TexCoord;              // <--- NUEVO

out vec4 FragColor;

uniform sampler2D uTexture;    // <--- NUEVO sampler de textura

void main()
{
    vec3 light = normalize(vec3(0.3, 0.5, 0.8));
    float diff = max(dot(normalize(Normal), light), 0.0);

    vec3 texColor = texture(uTexture, TexCoord).rgb;  // <--- LEE TEXTURA
    vec3 finalColor = texColor * (0.4 + diff * 0.6);  // <--- APLICA ILUMINACIÓN

    FragColor = vec4(finalColor, 1.0);
}
)";

// ============================================================
// SHADERS PARA DIBUJAR NORMALES
// ============================================================
const char* normalVertexShaderSrc = R"(
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uNormalLength;

out vec3 vStart;
out vec3 vEnd;

void main()
{
    vStart = vec3(uModel * vec4(aPos, 1.0));
    vEnd = vec3(uModel * vec4(aPos + aNormal * uNormalLength, 1.0));
}
)";

const char* normalGeometryShaderSrc = R"(
#version 460 core
layout (points) in;
layout (line_strip, max_vertices = 2) out;

in vec3 vStart[];
in vec3 vEnd[];

uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * uView * vec4(vStart[0], 1.0);
    EmitVertex();
    gl_Position = uProjection * uView * vec4(vEnd[0], 1.0);
    EmitVertex();
    EndPrimitive();
}
)";

const char* normalFragmentShaderSrc = R"(
#version 460 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 1.0, 0.0, 1.0); // verde brillante: mas contraste que el rojo sobre materiales calidos
}
)";

const char* gridVertexShaderSrc = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProjection;
void main()
{
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
)";

const char* gridFragmentShaderSrc = R"(
#version 460 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.6, 0.6, 0.6, 1.0);
}
)";


// ============================================================
// CONTROL DE CÁMARA
// ============================================================
glm::vec3 cameraPos = glm::vec3(0.0f, 2.0f, 5.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

float cameraSpeed = 3.0f;                       // unidades de mundo por segundo
const float CAMERA_SPRINT_MULTIPLIER = 3.0f;    // factor al mantener Shift
const float CAMERA_MAX_DELTA_TIME = 0.1f;       // s; acota tirones para no teletransportar la cámara
float yaw = -90.0f;
float pitch = 0.0f;
bool firstMouse = true;
float mouseSensitivity = 0.1f;
int selectedObject = -1;
bool showNormals = false;
Uint32 lastNormalsToggleMs = 0;
const Uint32 NORMALS_TOGGLE_DEBOUNCE = 150;
const float NORMAL_LENGTH_PERCENT = 0.03f; // % de la diagonal del AABB local de la malla

// --- Cámara orbital (Alt + click izquierdo) ---
const float CAMERA_FOV_DEG = 60.0f;             // FOV vertical; lo usan la proyección y el encuadre (F)
const float ORBIT_ZOOM_STEP = 0.1f;             // fracción de distance por muesca de rueda
const float ORBIT_MIN_RADIUS_FACTOR = 0.75f;    // distancia mínima = factor * radio del objeto...
const float ORBIT_MIN_DISTANCE_FLOOR = 0.25f;   // ...con este suelo si no hay objeto
const float ORBIT_MAX_DISTANCE = 90.0f;         // el far plane está en 100
const float FRAME_MARGIN = 1.2f;                // holgura alrededor del objeto al encuadrar
glm::vec3 orbitTarget = glm::vec3(0.0f);
float orbitDistance = 5.0f;                     // distancia cámara-target
float orbitRadius = 0.0f;                       // semidiagonal del AABB del target (0 = origen)

// Recalcula cameraFront a partir de yaw/pitch (compartido por libre y orbital).
static void UpdateCameraFront()
{
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

// Inversa de UpdateCameraFront: orienta yaw/pitch para mirar de "from" hacia "to".
static void AimCameraAt(const glm::vec3& from, const glm::vec3& to)
{
    glm::vec3 d = glm::normalize(to - from);
    yaw = glm::degrees(atan2(d.z, d.x));
    pitch = glm::clamp(glm::degrees(asin(d.y)), -89.0f, 89.0f);
    UpdateCameraFront();
}

// Target orbital = centro del AABB de mundo del objeto seleccionado; sin selección, el origen.
static void GetOrbitTarget(std::vector<GameObject>& objs, int sel, glm::vec3& outCenter, float& outRadius)
{
    if (sel >= 0 && sel < (int)objs.size())
    {
        GameObject& go = objs[sel];
        go.UpdateWorldAABB(); // defensivo: garantiza un AABB fresco aunque otra ruta haya tocado el transform
        outCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
        outRadius = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
    }
    else
    {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.0f;
    }
}

static float OrbitMinDistance(float radius)
{
    return std::max(ORBIT_MIN_DISTANCE_FLOOR, radius * ORBIT_MIN_RADIUS_FACTOR);
}

// ============================================================
// INICIALIZACIÓN DE SDL + OpenGL
// ============================================================
bool InitOpenGL(SDL_Window** window, SDL_GLContext* context, int width, int height)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "Error inicializando SDL3: " << SDL_GetError() << std::endl;
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
        "Motor - Carga de modelo FBX",
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!*window)
    {
        std::cerr << "Error creando ventana SDL3: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "Ventana creada correctamente." << std::endl;

    *context = SDL_GL_CreateContext(*window);
    if (!*context)
    {
        std::cerr << "Error creando contexto OpenGL: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!SDL_GL_SetSwapInterval(1))
    {
        std::cerr << "Error configurando Swap Interval: " << SDL_GetError() << std::endl;
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        std::cerr << "Error cargando funciones GLAD: " << SDL_GetError() << std::endl;
        return false;
    }

    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);

    std::cout << "OpenGL " << glGetString(GL_VERSION) << " inicializado correctamente." << std::endl;

    return true;
}

// ============================================================
// COMPILAR Y CREAR SHADER PROGRAM
// ============================================================
GLuint CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc)
{
    auto CompileShader = [](GLenum type, const char* src) -> GLuint
        {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                char info[512];
                glGetShaderInfoLog(shader, 512, NULL, info);
                std::cerr << " Error compilando shader: " << info << std::endl;
            }
            return shader;
        };

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetProgramInfoLog(program, 512, NULL, info);
        std::cerr << " Error linkeando shader program: " << info << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program;
}


GLuint CreateNormalShaderProgram()
{
    auto CompileShader = [](GLenum type, const char* src) -> GLuint
        {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                char info[512];
                glGetShaderInfoLog(shader, 512, NULL, info);
                std::cerr << " Error compilando shader: " << info << std::endl;
            }
            return shader;
        };

    GLuint vs = CompileShader(GL_VERTEX_SHADER, normalVertexShaderSrc);
    GLuint gs = CompileShader(GL_GEOMETRY_SHADER, normalGeometryShaderSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, normalFragmentShaderSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, gs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetProgramInfoLog(program, 512, NULL, info);
        std::cerr << " Error linkeando shader de normales: " << info << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(gs);
    glDeleteShader(fs);

    return program;
}

bool RayIntersectsAABB(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& aabbMin, const glm::vec3& aabbMax,
    float& outT)
{
    float tmin = (aabbMin.x - rayOrig.x) / (rayDir.x == 0.0f ? 1e-8f : rayDir.x);
    float tmax = (aabbMax.x - rayOrig.x) / (rayDir.x == 0.0f ? 1e-8f : rayDir.x);
    if (tmin > tmax) std::swap(tmin, tmax);

    float tymin = (aabbMin.y - rayOrig.y) / (rayDir.y == 0.0f ? 1e-8f : rayDir.y);
    float tymax = (aabbMax.y - rayOrig.y) / (rayDir.y == 0.0f ? 1e-8f : rayDir.y);
    if (tymin > tymax) std::swap(tymin, tymax);

    if ((tmin > tymax) || (tymin > tmax)) return false;

    if (tymin > tmin) tmin = tymin;
    if (tymax < tmax) tmax = tymax;

    float tzmin = (aabbMin.z - rayOrig.z) / (rayDir.z == 0.0f ? 1e-8f : rayDir.z);
    float tzmax = (aabbMax.z - rayOrig.z) / (rayDir.z == 0.0f ? 1e-8f : rayDir.z);
    if (tzmin > tzmax) std::swap(tzmin, tzmax);

    if ((tmin > tzmax) || (tzmin > tmax)) return false;

    if (tzmin > tmin) tmin = tzmin;
    if (tzmax < tmax) tmax = tzmax;
    if (tmax < 0.0f) return false; 

    outT = (tmin >= 0.0f) ? tmin : tmax; 
    return true;
}

struct Frustum
{
    glm::vec4 planes[6]; 
};

// Planos del frustum a partir de la matriz view-projection (metodo de Gribb/Hartmann).
// Con clip = vp * (x,y,z,1), un punto esta dentro si -w <= x,y,z <= w; cada desigualdad es un
// plano, p. ej. w + x >= 0 (izquierdo) = (fila3 + fila0) . (x,y,z,1) >= 0. Los planos salen de
// las FILAS de vp. GLM guarda las matrices por columnas (vp[i] es la columna i), asi que se
// transpone para que rows[i] sea la fila i. Profundidad de OpenGL por defecto (-w <= z <= w).
Frustum ExtractFrustum(const glm::mat4& vp)
{
    Frustum f;
    const glm::mat4 rows = glm::transpose(vp);
    f.planes[0] = rows[3] + rows[0]; // izquierdo
    f.planes[1] = rows[3] - rows[0]; // derecho
    f.planes[2] = rows[3] + rows[1]; // inferior
    f.planes[3] = rows[3] - rows[1]; // superior
    f.planes[4] = rows[3] + rows[2]; // cercano
    f.planes[5] = rows[3] - rows[2]; // lejano
    for (int i = 0; i < 6; ++i)
    {
        float len = glm::length(glm::vec3(f.planes[i]));
        f.planes[i] /= len;
    }

    return f;
}

bool AABBInsideFrustum(const Frustum& f,
    const glm::vec3& minB,
    const glm::vec3& maxB)
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec4& p = f.planes[i];

        glm::vec3 positive = minB;
        if (p.x >= 0) positive.x = maxB.x;
        if (p.y >= 0) positive.y = maxB.y;
        if (p.z >= 0) positive.z = maxB.z;

        if (glm::dot(glm::vec3(p), positive) + p.w < 0)
            return false;
    }
    return true;
}

void ComputeSceneAABB(const std::vector<GameObject>& objects,
    glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(FLT_MAX);
    outMax = glm::vec3(-FLT_MAX);

    for (const auto& go : objects)
    {
        glm::mat4 m = go.transform.GetMatrix();

        glm::vec3 corners[8] = {
            {go.aabbMin.x, go.aabbMin.y, go.aabbMin.z},
            {go.aabbMax.x, go.aabbMin.y, go.aabbMin.z},
            {go.aabbMin.x, go.aabbMax.y, go.aabbMin.z},
            {go.aabbMax.x, go.aabbMax.y, go.aabbMin.z},
            {go.aabbMin.x, go.aabbMin.y, go.aabbMax.z},
            {go.aabbMax.x, go.aabbMin.y, go.aabbMax.z},
            {go.aabbMin.x, go.aabbMax.y, go.aabbMax.z},
            {go.aabbMax.x, go.aabbMax.y, go.aabbMax.z}
        };

        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 w = glm::vec3(m * glm::vec4(corners[i], 1.0f));
            outMin = glm::min(outMin, w);
            outMax = glm::max(outMax, w);
        }
    }
    glm::vec3 size = outMax - outMin;
    outMin -= size * 0.1f;
    outMax += size * 0.1f;
}

// ============================================================
// CONVERTIR POSICIÓN DE RATÓN A RAYO EN ESPACIO MUNDIAL
// ============================================================
// Direccion del rayo que sale de la camara por el pixel (mouseX, mouseY) del viewport.
// mouseX/mouseY son PIXELES RELATIVOS a la esquina superior izquierda del viewport (Y hacia abajo,
// como los da ImGui); viewportW/H es su tamano en pixeles. glm::unProject espera coordenadas de
// ventana con el origen ABAJO a la izquierda y las compara con el viewport que se le pasa, asi que:
// el viewport es (0, 0, W, H) (el mouse ya es relativo; sumarle la posicion absoluta del viewport en
// pantalla desplazaba el rayo) y la Y se invierte UNA sola vez (H - mouseY).
glm::vec3 ScreenToWorldRay_UnProject(float mouseX, float mouseY,
    float viewportW, float viewportH,
    const glm::mat4& projection, const glm::mat4& view)
{
    const glm::vec4 viewport(0.0f, 0.0f, viewportW, viewportH);
    const float winY = viewportH - mouseY;
    glm::vec3 winNear = glm::unProject(glm::vec3(mouseX, winY, 0.0f), view, projection, viewport);
    glm::vec3 winFar = glm::unProject(glm::vec3(mouseX, winY, 1.0f), view, projection, viewport);
    return glm::normalize(winFar - winNear);
}

bool ComputeAABBFromMesh(const Mesh& mesh, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(0.0f);
    outMax = glm::vec3(0.0f);

    if (mesh.vertexCount == 0 || mesh.vertices.empty()) return false;

    size_t totalFloats = mesh.vertices.size();
    size_t vc = static_cast<size_t>(mesh.vertexCount);

    size_t stride = 0;
    if (vc > 0 && totalFloats % vc == 0)
    {
        stride = totalFloats / vc;
    }
    else if (totalFloats >= vc * 11)
    {
        stride = 11;
    }
    else if (totalFloats >= vc * 8) 
    {
        stride = 8;
    }
    else
    {
        Console::LogInfo("ComputeAABBFromMesh: imposible deducir stride (vertices.size=%zu, vertexCount=%zu)", totalFloats, vc);
        return false;
    }

    if (stride < 3)
    {
        Console::LogInfo("ComputeAABBFromMesh: stride detectado < 3, imposible (stride=%zu)", stride);
        return false;
    }

    size_t expected = vc * stride;
    if (totalFloats < expected)
    {
        Console::LogInfo("ComputeAABBFromMesh: datos insuficientes (got=%zu expected=%zu)", totalFloats, expected);
        return false;
    }

    glm::vec3 minB(std::numeric_limits<float>::infinity());
    glm::vec3 maxB(-std::numeric_limits<float>::infinity());

    for (size_t i = 0; i < vc; ++i)
    {
        size_t idx = i * stride;
        if (idx + 2 >= totalFloats) break;
        glm::vec3 pos(mesh.vertices[idx + 0], mesh.vertices[idx + 1], mesh.vertices[idx + 2]);
        minB = glm::min(minB, pos);
        maxB = glm::max(maxB, pos);
    }

    if (!std::isfinite(minB.x) || !std::isfinite(maxB.x))
    {
        Console::LogInfo("ComputeAABBFromMesh: fallo al calcular min/max");
        return false;
    }

    outMin = minB;
    outMax = maxB;
    return true;
}


// ============================================================
// PROGRAMA PRINCIPAL
// ============================================================
int main(int argc, char* argv[])
{

    Console::Init();

     stbi_set_flip_vertically_on_load(true);

    SDL_Window* window = nullptr;
    SDL_GLContext context;

    if (!InitOpenGL(&window, &context, 1280, 720))
    {
        Console::LogError("No se pudo inicializar OpenGL/SDL: %s", SDL_GetError());
        return -1;
    }

    Console::LogInfo("Inicializando motor - version E1");

    if (g_vsync) SDL_GL_SetSwapInterval(1);
    else SDL_GL_SetSwapInterval(0);

    g_lastFrameTick = SDL_GetPerformanceCounter();
    g_frameTimes.assign(PERF_HISTORY, 0.0f);


    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

    glm::mat4 projection = glm::perspective(glm::radians(CAMERA_FOV_DEG),
        1280.0f / 720.0f,
        0.1f, 100.0f);

    // ============================================================
    // INICIALIZAR IMGUI
    // ============================================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; 
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 460");

    GLuint shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
    GLuint normalShaderProgram = CreateNormalShaderProgram();
    GLuint gridShaderProgram = CreateShaderProgram(gridVertexShaderSrc, gridFragmentShaderSrc);

    // ============================================================
    // FRAMEBUFFER PARA EL VIEWPORT DE IMGUI
    // ============================================================
    GLuint viewportFBO = 0, viewportTexture = 0, viewportRBO = 0;
    int viewportWidth = 1280;
    int viewportHeight = 720;

    glGenFramebuffers(1, &viewportFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);

    glGenTextures(1, &viewportTexture);
    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, viewportWidth, viewportHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

    glGenRenderbuffers(1, &viewportRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, viewportRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth, viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, viewportRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << " Error: el framebuffer del viewport no esta completo!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(shaderProgram);

    // ============================================================
    // CREAR TEXTURA CHECKERBOARD
    // ============================================================
    const int CHECKERS_WIDTH = 64;
    const int CHECKERS_HEIGHT = 64;
    GLubyte checkerImage[CHECKERS_HEIGHT][CHECKERS_WIDTH][4];

    for (int i = 0; i < CHECKERS_HEIGHT; ++i)
    {
        for (int j = 0; j < CHECKERS_WIDTH; ++j)
        {
            int c = (((((i & 0x8) == 0) ^ ((j & 0x8) == 0)) ? 1 : 0)) * 255;
            checkerImage[i][j][0] = (GLubyte)c; // R
            checkerImage[i][j][1] = (GLubyte)c; // G
            checkerImage[i][j][2] = (GLubyte)c; // B
            checkerImage[i][j][3] = 255;        // A
        }
    }

    glGenTextures(1, &checkerTexture);
    glBindTexture(GL_TEXTURE_2D, checkerTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CHECKERS_WIDTH, CHECKERS_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, checkerImage);

    glBindTexture(GL_TEXTURE_2D, 0);

    // ========================================================
    // CREAR GRID (REJILLA DE REFERENCIA)
    // ========================================================
    GLuint gridVAO, gridVBO;
    std::vector<float> gridVertices;

    int gridSize = 20; 
    float spacing = 1.0f;

    for (int i = -gridSize; i <= gridSize; ++i)
    {
        // Líneas paralelas al eje X
        gridVertices.push_back((float)i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)-gridSize * spacing);

        gridVertices.push_back((float)i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)gridSize * spacing);


        gridVertices.push_back((float)-gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)i * spacing);

        gridVertices.push_back((float)gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)i * spacing);
    }

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    // ========================================================
    // ESCENA: sin carga automatica al arrancar.
    // Los modelos se cargan exclusivamente via drag & drop
    // (ver SDL_EVENT_DROP_FILE mas abajo).
    // ========================================================
    std::vector<GameObject> gameObjects;

    // CREAR OCTREE DE LA ESCENA
    glm::vec3 sceneMin, sceneMax;
    ComputeSceneAABB(gameObjects, sceneMin, sceneMax);

    // Octree global de escena
    Octree sceneOctree(sceneMin, sceneMax);

    // Insertar todos los GameObjects
    for (auto& go : gameObjects)
    {
        go.UpdateWorldAABB();          
        sceneOctree.Insert(&go);
    }


    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture"), 0);

    GLint uModelLoc = glGetUniformLocation(shaderProgram, "uModel");
    GLint uViewLoc = glGetUniformLocation(shaderProgram, "uView");
    GLint uProjLoc = glGetUniformLocation(shaderProgram, "uProjection");

    GLint normalUModelLoc = glGetUniformLocation(normalShaderProgram, "uModel");
    GLint normalUViewLoc = glGetUniformLocation(normalShaderProgram, "uView");
    GLint normalUProjLoc = glGetUniformLocation(normalShaderProgram, "uProjection");
    GLint normalULengthLoc = glGetUniformLocation(normalShaderProgram, "uNormalLength");
    // ========================================================
    // MATRICES DE CÁMARA
    // ========================================================

    bool running = true;
    SDL_Event e;

    bool showConsole = true;
    bool showInspector = true;
    bool showHierarchy = true;
    bool showConfig = true;
    bool showAbout = false;

    bool mouseInsideViewport = false;
    float relMouseX = 0.0f;
    float relMouseY = 0.0f;
    float viewportWidthF = 1280.0f;
    float viewportHeightF = 720.0f;

    int prevMouseX = 0, prevMouseY = 0;
    bool freeLookActive = false;   // el click derecho empezó dentro del viewport
    bool orbitActive = false;      // Alt + click izquierdo empezó dentro del viewport
    float wheelDelta = 0.0f;       // muescas de rueda acumuladas en el frame (dentro del viewport)

    // Captura del ratón compartida por cámara libre y orbital (son excluyentes).
    auto BeginMouseCapture = [&]() {
        float mx = 0.f, my = 0.f;
        SDL_GetMouseState(&mx, &my);
        prevMouseX = (int)mx;
        prevMouseY = (int)my;

        SDL_SetWindowRelativeMouseMode(window, true);
        SDL_HideCursor();
        firstMouse = true;
        };

    auto EndMouseCapture = [&]() {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_ShowCursor();
        SDL_WarpMouseInWindow(window, (float)prevMouseX, (float)prevMouseY);
        };

    // Aplica el delta de ratón a yaw/pitch (mismos ángulos en ambos modos).
    auto ApplyMouseLook = [&]() {
        float dx = 0.f, dy = 0.f;
        SDL_GetRelativeMouseState(&dx, &dy);
        // SDL acumula el movimiento desde la última llamada, y aquí solo se
        // llama con un botón mantenido: el primer delta trae todo el recorrido
        // del cursor hasta el viewport. Se descarta.
        if (firstMouse)
        {
            dx = dy = 0.f;
            firstMouse = false;
        }
        yaw += dx * mouseSensitivity;
        pitch += -dy * mouseSensitivity;
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        UpdateCameraFront();
        };

    // F: encuadra el objeto seleccionado manteniendo la dirección de mirada actual.
    auto FrameSelectedObject = [&]() {
        if (selectedObject < 0 || selectedObject >= (int)gameObjects.size())
        {
            Console::LogInfo("F: no hay ningun objeto seleccionado que encuadrar");
            return;
        }
        GetOrbitTarget(gameObjects, selectedObject, orbitTarget, orbitRadius);

        // La esfera que envuelve el AABB debe caber en el FOV mas estrecho
        // (vertical u horizontal, segun el aspecto del viewport).
        float aspect = viewportHeightF > 0.0f ? viewportWidthF / viewportHeightF : 1.0f;
        float halfFovY = glm::radians(CAMERA_FOV_DEG) * 0.5f;
        float halfFovX = atan(tan(halfFovY) * aspect);
        float halfFov = std::min(halfFovY, halfFovX);

        float radius = std::max(orbitRadius, 0.01f);
        orbitDistance = radius / sin(halfFov) * FRAME_MARGIN;
        orbitDistance = glm::clamp(orbitDistance, OrbitMinDistance(orbitRadius), ORBIT_MAX_DISTANCE);
        cameraPos = orbitTarget - cameraFront * orbitDistance;

        Console::LogInfo("Encuadrado %s: target=(%.2f, %.2f, %.2f) distancia=%.2f",
            gameObjects[selectedObject].name.c_str(), orbitTarget.x, orbitTarget.y, orbitTarget.z, orbitDistance);
        };

    // El octree guarda GameObject* directos a los elementos de gameObjects (std::vector por
    // valor). Cualquier insert/erase/push_back invalida esos punteros (reasigna el buffer o
    // desplaza y destruye elementos), asi que toda mutacion estructural del vector debe
    // marcar este flag; el bucle principal reconstruye el octree antes de volver a usarlo.
    bool sceneStructureDirty = false;

    auto DuplicateGameObject = [&](std::vector<GameObject>& list, int index) -> int {
        if (index < 0 || index >= (int)list.size()) return -1;
        GameObject copy = list[index];
        // La copia de Mesh duplica tambien los handles de GL (VAO/VBO/EBO): original y copia
        // apuntarian a los mismos buffers y borrar uno dejaria al otro sin geometria.
        // SetupMesh genera buffers propios a partir de vertices/indices, que si se copiaron.
        copy.meshComp.mesh.SetupMesh();
        copy.name = list[index].name + "_copy";
        copy.transform.position += glm::vec3(0.1f, 0.0f, 0.1f);
        list.insert(list.begin() + index + 1, std::move(copy));
        sceneStructureDirty = true;
        return index + 1;
        };

    auto EraseGameObjectAt = [&](std::vector<GameObject>& list, int index, int& selected) -> void {
        if (index < 0 || index >= (int)list.size()) return;
        const GLuint tex = list[index].texComp.textureID;
        list[index].Destroy();
        list.erase(list.begin() + index);
        sceneStructureDirty = true;

        // La textura puede estar compartida con un duplicado: solo se borra si ya no la usa
        // ningun otro objeto (y nunca la checker, que es global).
        if (tex != 0 && tex != checkerTexture &&
            std::none_of(list.begin(), list.end(), [tex](const GameObject& o) { return o.texComp.textureID == tex; }))
        {
            glDeleteTextures(1, &tex);
        }
        if (selected == index) {
            selected = -1;
        }
        else if (selected > index) {
            selected = selected - 1;
        }
        };

    auto RebuildOctree = [&]()
        {
            glm::vec3 minB, maxB;

            if (gameObjects.empty())
            {
                // ComputeSceneAABB no tiene objetos de los que sacar limites
                // reales (degeneraria a +-infinito). Usamos los mismos limites
                // fijos que ya se usan para la query de dibujado (frustumMin/Max).
                minB = glm::vec3(-1000.0f);
                maxB = glm::vec3(1000.0f);
            }
            else
            {
                ComputeSceneAABB(gameObjects, minB, maxB);
            }

            sceneOctree.Rebuild(minB, maxB);

            for (auto& go : gameObjects)
            {
                go.UpdateWorldAABB();
                sceneOctree.Insert(&go);
            }

            Console::LogInfo("Octree rebuilt (%zu objects)", gameObjects.size());
        };



    // ========================================================
    // BUCLE PRINCIPAL
    // ========================================================
    std::cout << " Iniciando bucle principal..." << std::endl;

    while (running)
    {
        // ========================================================
        // MANEJO DE EVENTOS SDL + IMGUI
        // ========================================================
        while (SDL_PollEvent(&e))
        {
            ImGui_ImplSDL3_ProcessEvent(&e);

            switch (e.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            {
                int sc = e.key.scancode;
                if (sc == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                else if (sc == SDL_SCANCODE_H)
                {
                    if (selectedObject >= 0 && selectedObject < static_cast<int>(gameObjects.size()))
                    {
                        std::cout << "[H] toggle on index " << selectedObject << " / size=" << gameObjects.size() << std::endl;

                        gameObjects[selectedObject].hidden = !gameObjects[selectedObject].hidden;

                        if (gameObjects[selectedObject].hidden)
                        {
                            std::cout << gameObjects[selectedObject].name << " ocultado." << std::endl;
                            selectedObject = -1; 
                        }
                        else
                        {
                            std::cout << gameObjects[selectedObject].name << " mostrado." << std::endl;
                        }
                    }
                    else
                    {
                        std::cerr << "[H] Advertencia: selectedObject inválido: " << selectedObject
                            << " (size=" << gameObjects.size() << ")" << std::endl;
                    }
                }
                else if (sc == SDL_SCANCODE_N)
                {
                    Uint32 now = SDL_GetTicks();
                    if (now - lastNormalsToggleMs > NORMALS_TOGGLE_DEBOUNCE) {
                        showNormals = !showNormals;
                        std::cout << "Mostrar normales: " << (showNormals ? "ON" : "OFF") << std::endl;
                        lastNormalsToggleMs = now;
                    }
                }
                else if (sc == SDL_SCANCODE_F && !io.WantTextInput)
                {
                    FrameSelectedObject();
                }
            }
            break;

            case SDL_EVENT_MOUSE_WHEEL:
                // Solo con el cursor sobre el viewport (dato del frame anterior, como el resto).
                if (mouseInsideViewport)
                    wheelDelta += e.wheel.y;
                break;

            case SDL_EVENT_DROP_FILE:
            {
                const char* dropped_filedir = e.drop.data;
                std::string filePath = dropped_filedir ? dropped_filedir : "";

                if (!filePath.empty())
                {
                    Console::LogInfo("Archivo arrastrado: %s", filePath.c_str());

                    std::string lower = filePath;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return std::tolower(c); });
                    auto ends_with = [](const std::string& str, const std::string& suffix)
                        {
                            return str.size() >= suffix.size() &&
                                str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
                        };

                    if (ends_with(lower, ".png") || ends_with(lower, ".jpg") ||
                        ends_with(lower, ".jpeg") || ends_with(lower, ".tga"))
                    {
                        int texWidth, texHeight, texChannels;
                        unsigned char* data = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, 0);
                        if (data)
                        {
                            GLuint newTex;
                            glGenTextures(1, &newTex);
                            glBindTexture(GL_TEXTURE_2D, newTex);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                            GLenum format = (texChannels == 4) ? GL_RGBA : GL_RGB;
                            glTexImage2D(GL_TEXTURE_2D, 0, format, texWidth, texHeight, 0, format, GL_UNSIGNED_BYTE, data);
                            glGenerateMipmap(GL_TEXTURE_2D);
                            stbi_image_free(data);

                            if (selectedObject >= 0 && selectedObject < (int)gameObjects.size())
                            {
                                gameObjects[selectedObject].texComp.textureID = newTex;
                                gameObjects[selectedObject].texComp.path = filePath;
                                Console::LogInfo("Textura aplicada a: %s (path: %s)", gameObjects[selectedObject].name.c_str(), filePath.c_str());
                            }
                            else if (!gameObjects.empty())
                            {
                                gameObjects[0].texComp.textureID = newTex;
                                gameObjects[0].texComp.path = filePath;
                                Console::LogInfo("Textura aplicada al primer objeto: %s (path: %s)", gameObjects[0].name.c_str(), filePath.c_str());
                            }

                            else
                            {
                                std::cout << "No hay GameObjects para aplicar la textura." << std::endl;
                            }
                        }
                        else
                        {
                            std::cerr << "Error cargando imagen: " << filePath << std::endl;
                        }
                    }
                    else if (ends_with(lower, ".fbx"))
                    {
                        float fbxUnitScale = 1.0f;
                        std::vector<Mesh> newMeshes = LoadModel(filePath, &fbxUnitScale);

                        size_t currentSize = gameObjects.size();
                        for (size_t i = 0; i < newMeshes.size(); ++i)
                        {
                            GameObject go;
                     
                            size_t lastSlash = filePath.find_last_of("/\\");
                            size_t lastDot = filePath.find_last_of('.');
                            std::string baseName = filePath.substr(
                                lastSlash == std::string::npos ? 0 : lastSlash + 1,
                                (lastDot == std::string::npos ? filePath.size() : lastDot) - (lastSlash + 1)
                            );

                            go.name = baseName + "_" + std::to_string(i + currentSize);
                            go.meshComp.mesh = std::move(newMeshes[i]);

                            glm::vec3 minB, maxB;
                            if (ComputeAABBFromMesh(go.meshComp.mesh, minB, maxB)) {
                                go.aabbMin = minB;
                                go.aabbMax = maxB;
                            }
                            else {
                                go.aabbMin = glm::vec3(0.0f);
                                go.aabbMax = glm::vec3(0.0f);
                                Console::LogInfo("Inicial AABB fallback para %s", go.name.c_str());
                            }

                            go.transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
                            go.transform.rotation = glm::vec3(0.0f);
                            go.transform.scale = glm::vec3(fbxUnitScale);

                            if (go.meshComp.mesh.textureID != 0)
                            {
                                go.texComp.textureID = go.meshComp.mesh.textureID;
                                go.texComp.path = go.meshComp.mesh.texturePath;
                            }
                            else if (!go.meshComp.mesh.texturePath.empty())
                            {
                                // mesh.texturePath viene de Assimp tal como esta guardado en el
                                // material del FBX: normalmente solo el nombre de archivo, sin
                                // directorio. Se recompone contra la carpeta del .fbx soltado
                                // (filePath), no contra el CWD del proceso.
                                std::string textureDir = (lastSlash == std::string::npos)
                                    ? std::string()
                                    : filePath.substr(0, lastSlash + 1);
                                std::string textureFullPath = textureDir + go.meshComp.mesh.texturePath;

                                int w, h, channels;
                                unsigned char* img = stbi_load(textureFullPath.c_str(), &w, &h, &channels, 0);
                                if (img)
                                {
                                    GLuint tex;
                                    glGenTextures(1, &tex);
                                    glBindTexture(GL_TEXTURE_2D, tex);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                                    GLenum format = (channels == 4) ? GL_RGBA : GL_RGB;
                                    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, img);
                                    glGenerateMipmap(GL_TEXTURE_2D);
                                    stbi_image_free(img);

                                    go.texComp.textureID = tex;
                                    go.texComp.path = textureFullPath;
                                    go.meshComp.mesh.textureID = tex;
                                }
                                else
                                {
                                    go.texComp.textureID = checkerTexture;
                                    go.texComp.path.clear();
                                    Console::LogInfo("No se pudo cargar textura desde %s", textureFullPath.c_str());
                                }
                            }
                            else
                            {
                                go.texComp.textureID = checkerTexture;
                                go.texComp.path.clear();
                            }

                            gameObjects.push_back(std::move(go));
                        }
                        RebuildOctree();
                        std::cout << "FBX cargado: " << filePath << std::endl;
                    }

                    else
                    {
                        std::cout << "Archivo no soportado: " << filePath << std::endl;
                    }
                }
            }

            }
        }
        // ========================================================
        //  INICIAR FRAME IMGUI (OBLIGATORIO)
        // ========================================================
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ============================================================
        // Barra de menú principal
        // ============================================================
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Console", nullptr, &showConsole);
                ImGui::MenuItem("Inspector", nullptr, &showInspector);
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
                ImGui::MenuItem("Configuration", nullptr, &showConfig);
                ImGui::MenuItem("Normals", nullptr, &showNormals);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Documentation")) SDL_OpenURL("https://github.com/TuRepo/docs");
                if (ImGui::MenuItem("Report Bug")) SDL_OpenURL("https://github.com/TuRepo/issues");
                if (ImGui::MenuItem("Download Latest")) SDL_OpenURL("https://github.com/TuRepo/releases");
                if (ImGui::MenuItem("About")) showAbout = true;
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
        if (showAbout)
        {
            ImGui::Begin("About", &showAbout);
            ImGui::Text("Motor 3D v1.0");
            ImGui::Separator();
            ImGui::Text("Autores: Albert Frederic Bailen Ruesca, ...");
            ImGui::Text("Librerias usadas: SDL3, OpenGL, ImGui, Assimp, stb_image, GLM");
            ImGui::Text("Licencia: MIT License");
            ImGui::End();
        }

        // ========================================================
        //  CONTROL DE CÁMARA: LIBRE (click derecho) Y ORBITAL (Alt + click izquierdo)
        // ========================================================
        bool rightClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool rightClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        bool rightClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
        bool leftClickHeld = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool leftClickPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool leftClickReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        bool altHeld = io.KeyAlt; // el picking (más abajo) lo consulta: Alt + click izquierdo nunca selecciona

        // Entrada en cada modo. Ambos capturan el ratón, así que son excluyentes.
        // mouseInsideViewport lo calcula el panel "Viewport" más abajo en el
        // frame, así que aquí vale el del frame anterior (mismo dato que usa el picking).
        if (mouseInsideViewport && !freeLookActive && !orbitActive)
        {
            if (rightClickPressed)
            {
                BeginMouseCapture();
                freeLookActive = true;
            }
            else if (leftClickPressed && altHeld)
            {
                BeginMouseCapture();
                orbitActive = true;

                // Target: objeto seleccionado (o el origen). distance: la actual cámara-target.
                // Se orienta la cámara hacia el target SIN moverla, así cameraPos ya cumple
                // cameraPos = target - front * distance y no hay salto de posición.
                GetOrbitTarget(gameObjects, selectedObject, orbitTarget, orbitRadius);
                orbitDistance = glm::length(cameraPos - orbitTarget);
                if (orbitDistance > 1e-4f) AimCameraAt(cameraPos, orbitTarget);
                orbitDistance = glm::clamp(orbitDistance, OrbitMinDistance(orbitRadius), ORBIT_MAX_DISTANCE);
                cameraPos = orbitTarget - cameraFront * orbitDistance;
            }
        }

        if (rightClickReleased && freeLookActive)
        {
            EndMouseCapture();
            freeLookActive = false;
        }

        if (leftClickReleased && orbitActive)
        {
            EndMouseCapture();
            orbitActive = false;
        }

        if (freeLookActive && rightClickHeld)
        {
            io.WantCaptureMouse = false;
            io.WantCaptureKeyboard = false;

            ApplyMouseLook();

            // Duración del último frame completo (g_frameTimes está en ms).
            float deltaTime = g_frameTimes.empty() ? 0.0f : g_frameTimes.back() / 1000.0f;
            deltaTime = std::min(deltaTime, CAMERA_MAX_DELTA_TIME);

            const bool* keys = SDL_GetKeyboardState(NULL);
            float speed = cameraSpeed * deltaTime;
            if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) speed *= CAMERA_SPRINT_MULTIPLIER;

            // Se suma la dirección de cada tecla y se normaliza el conjunto, para que
            // combinar teclas (p. ej. W+D) no sea ×√2 más rápido que ir en línea recta.
            // Teclas opuestas se anulan (vector cero: no hay movimiento).
            glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
            glm::vec3 moveDir(0.0f);
            if (keys[SDL_SCANCODE_W]) moveDir += cameraFront;
            if (keys[SDL_SCANCODE_S]) moveDir -= cameraFront;
            if (keys[SDL_SCANCODE_A]) moveDir -= right;
            if (keys[SDL_SCANCODE_D]) moveDir += right;
            if (keys[SDL_SCANCODE_E]) moveDir += cameraUp;
            if (keys[SDL_SCANCODE_Q]) moveDir -= cameraUp;
            if (glm::length(moveDir) > 1e-6f) cameraPos += glm::normalize(moveDir) * speed;
        }

        if (orbitActive && leftClickHeld)
        {
            ApplyMouseLook();
            cameraPos = orbitTarget - cameraFront * orbitDistance;
        }

        // Rueda: acerca/aleja la cámara del target. En modo orbital cambia distance y recoloca
        // la cámara sobre la esfera; fuera de él es un dolly a lo largo de cameraFront (sin
        // girar la vista), con la distancia real al target como referencia.
        if (wheelDelta != 0.0f)
        {
            if (!orbitActive)
            {
                GetOrbitTarget(gameObjects, selectedObject, orbitTarget, orbitRadius);
                orbitDistance = glm::length(cameraPos - orbitTarget);
            }
            float oldDistance = orbitDistance;
            float newDistance = oldDistance * std::pow(1.0f - ORBIT_ZOOM_STEP, wheelDelta);
            // Sin atravesar el objeto ni salir del far plane, pero sin atrapar a una
            // cámara que ya está fuera de ese rango: solo se limita el sentido que empeora.
            if (newDistance < oldDistance)
                newDistance = std::max(newDistance, std::min(oldDistance, OrbitMinDistance(orbitRadius)));
            else
                newDistance = std::min(newDistance, std::max(oldDistance, ORBIT_MAX_DISTANCE));
            orbitDistance = newDistance;

            if (orbitActive) cameraPos = orbitTarget - cameraFront * orbitDistance;
            else cameraPos += cameraFront * (oldDistance - newDistance);
        }
        wheelDelta = 0.0f;

        // Único punto donde se recalcula view: refleja cameraPos/cameraFront tras cualquier
        // modo, la rueda o F. El picking usa cameraPos como origen del rayo, y debe coincidir.
        view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

        glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);


        glViewport(0, 0, viewportWidth, viewportHeight);
        glClearColor(g_clearColor.x, g_clearColor.y, g_clearColor.z, g_clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ============================================================
        //  Actualizar proyección según el tamaño del viewport
        // ============================================================
        projection = glm::perspective(
            glm::radians(CAMERA_FOV_DEG),
            viewportWidthF / viewportHeightF,
            0.1f, 100.0f
        );

        // ----------------------- DOCKSPACE PRINCIPAL -----------------------
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        static bool first_time = true;
        if (first_time)
        {
            first_time = false;

            ImGui::DockBuilderRemoveNode(dockspace_id); 
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

            ImGuiID dock_main_id = dockspace_id;
            ImGuiID dock_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
            ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

            ImGui::DockBuilderDockWindow("Viewport", dock_main_id);      // centro
            ImGui::DockBuilderDockWindow("Hierarchy", dock_left);        // izquierda
            ImGui::DockBuilderDockWindow("Inspector", dock_right);       // derecha

            ImGui::DockBuilderDockWindow("Configuration", dock_bottom);
            ImGui::DockBuilderDockWindow("Console", dock_bottom);        // abajo
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::End();

        // -------------------- INSPECTOR (MEJORADO: COMPONENTS) --------------------
        if (showInspector)
        {
            ImGui::Begin("Inspector");

            if (selectedObject >= 0 && selectedObject < (int)gameObjects.size())
            {
                GameObject& go = gameObjects[selectedObject];
                char nameBuf[128];
                std::strncpy(nameBuf, go.name.c_str(), sizeof(nameBuf));
                nameBuf[sizeof(nameBuf) - 1] = '\0';
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
                {
                    go.name = std::string(nameBuf);
                }

                ImGui::Separator();

                // ---------------- Transform ----------------
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    // Cualquier cambio del transform deja desactualizado el AABB de mundo cacheado
                    // (worldAabbMin/Max), que usan el culling y el picking, y la posicion del
                    // objeto dentro del octree.
                    bool transformChanged = false;    // hubo un cambio de valor en este frame
                    bool transformCommitted = false;  // el gesto de edicion (arrastre o tecleo) ha terminado

                    if (ImGui::DragFloat3("Position", glm::value_ptr(go.transform.position), 0.1f))
                    {
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(go.transform.rotation), 0.5f))
                    {
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::DragFloat3("Scale", glm::value_ptr(go.transform.scale), 0.05f))
                    {
                        go.transform.scale = glm::max(go.transform.scale, glm::vec3(0.001f));
                        transformChanged = true;
                    }
                    transformCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::Button("Reset Transform")) {
                        go.transform.position = glm::vec3(0.0f);
                        go.transform.rotation = glm::vec3(0.0f);
                        go.transform.scale = glm::vec3(1.0f);
                        transformChanged = true;
                        transformCommitted = true;
                    }

                    // El AABB del objeto se refresca en cada cambio (8 esquinas, barato), asi el
                    // culling y el picking de este mismo frame ya usan la posicion real...
                    if (transformChanged)
                    {
                        go.UpdateWorldAABB();
                    }
                    // ...pero el octree solo sabe reconstruirse entero (y cada reconstruccion escribe
                    // en la consola): se reconstruye una vez al terminar el gesto, no en cada frame
                    // de un arrastre. Mientras se arrastra, la consulta de dibujo (caja de +-1000)
                    // sigue devolviendo el objeto aunque su hoja del octree sea la antigua.
                    if (transformCommitted)
                    {
                        sceneStructureDirty = true;
                    }
                }

                ImGui::Separator();

                // ---------------- Mesh ----------------
                if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const Mesh& mesh = go.meshComp.mesh;
                    ImGui::Text("Vertices: %u", mesh.vertexCount);
                    ImGui::Text("Indices: %u", mesh.indexCount);
                    ImGui::Text("VAO: %u", mesh.VAO);
                    ImGui::Text("TexturePath: %s", mesh.texturePath.empty() ? "<none>" : mesh.texturePath.c_str());
 
                    static const char* normalsModes[] = {
                         "Use Global",
                         "Force On",
                         "Force Off"
                    };
                    int mode = static_cast<int>(go.normalsMode);
                    if (ImGui::Combo("Normals Mode", &mode, normalsModes, IM_ARRAYSIZE(normalsModes)))
                    {
                        go.normalsMode = static_cast<GameObject::NormalsMode>(mode);
                    }
                    bool forceOnNormals = (go.normalsMode == GameObject::NM_ForceOn);
                    if (ImGui::Checkbox("Mostrar normales (mesh)", &forceOnNormals))
                    {
                        go.normalsMode = forceOnNormals ? GameObject::NM_ForceOn : GameObject::NM_UseGlobal;
                        Console::LogInfo("Mostrar normales (local) de %s: %s", go.name.c_str(), normalsModes[go.normalsMode]);
                    }

                    ImGui::TextDisabled("Global Normals: %s", showNormals ? "ON" : "OFF");

                    if (ImGui::Button("Recalcular AABB")) {
                      
                        glm::vec3 minB, maxB;
                        if (ComputeAABBFromMesh(mesh, minB, maxB))
                        {
                            go.aabbMin = minB;
                            go.aabbMax = maxB;
                            Console::LogInfo("AABB recalculada para %s", go.name.c_str());
                        }
                        else
                        {
                            Console::LogInfo("Recalculo AABB fallido para %s (mesh datos insuficientes)", go.name.c_str());
                        }
                        RebuildOctree();

                    }
                }

                ImGui::Separator();

                // ---------------- Texture ----------------
                if (ImGui::CollapsingHeader("Texture", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("TextureID: %u", go.texComp.textureID);
                    ImGui::TextWrapped("Path: %s", go.texComp.path.empty() ? "<none>" : go.texComp.path.c_str());

                    if (go.texComp.textureID != 0)
                    {
                        ImGui::Image((ImTextureID)(intptr_t)go.texComp.textureID, ImVec2(128, 128), ImVec2(0, 1), ImVec2(1, 0));
                        if (ImGui::Button("Remove Texture"))
                        {
                            go.texComp.textureID = 0;
                            go.texComp.path.clear();
                            Console::LogInfo("Texture removed from %s", go.name.c_str());
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Open Folder")) {
                            if (!go.texComp.path.empty()) {
                                std::string cmd = std::string("explorer /select,\"") + go.texComp.path + "\"";
                                system(cmd.c_str());
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("No texture assigned");
                    }

                    if (ImGui::Button("Assign CheckerTexture"))
                    {
                        extern GLuint checkerTexture; 
                        go.texComp.textureID = checkerTexture;
                        go.texComp.path.clear();
                        Console::LogInfo("Checker texture asignada a %s", go.name.c_str());
                    }
                }

            }
            else
            {
                ImGui::Text("No hay ningun objeto seleccionado");
            }

            ImGui::End();
        }
        // ========================= Configuration Window =========================
        if (showConfig) {
            ImGui::Begin("Configuration", &showConfig);

            if (ImGui::CollapsingHeader("System / Graphics Info", ImGuiTreeNodeFlags_DefaultOpen)) {

                ImGui::Text("SDL Version: %d.%d.%d",
                    SDL_MAJOR_VERSION,
                    SDL_MINOR_VERSION,
                    SDL_MICRO_VERSION);

                ImGui::Text("CPU Cores: %d", SDL_GetNumLogicalCPUCores());
                ImGui::Text("CPU Cacheline: %d bytes", SDL_GetCPUCacheLineSize());
                ImGui::Text("Has RDTSC: N/A (SDL3 build without CPU feature API)");
                ImGui::Text("System RAM: %d MB", SDL_GetSystemRAM());

                // OpenGL / GPU
                const GLubyte* glVendor = glGetString(GL_VENDOR);
                const GLubyte* glRenderer = glGetString(GL_RENDERER);
                const GLubyte* glVersion = glGetString(GL_VERSION);
                const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
                ImGui::Separator();
                ImGui::Text("GPU Vendor: %s", glVendor ? (const char*)glVendor : "unknown");
                ImGui::Text("GPU Renderer: %s", glRenderer ? (const char*)glRenderer : "unknown");
                ImGui::Text("OpenGL Version: %s", glVersion ? (const char*)glVersion : "unknown");
                ImGui::Text("GLSL Version: %s", glslVersion ? (const char*)glslVersion : "unknown");

                // Assimp version
                #ifdef ASSIMP_VERSION_MAJOR
                ImGui::Text("Assimp: %d.%d.%d", ASSIMP_VERSION_MAJOR, ASSIMP_VERSION_MINOR, ASSIMP_VERSION_REVISION);
                #else
                #if defined(AI_CONFIG_BOOTSTRAP)
                ImGui::Text("Assimp: (AI macros available)");
                #else
                #ifdef aiGetVersionMajor
                ImGui::Text("Assimp: %d.%d.%d", aiGetVersionMajor(), aiGetVersionMinor(), aiGetVersionRevision());
                #else
                ImGui::Text("Assimp: (version unknown)");
                #endif
                #endif
                #endif

                ImGui::Separator();
                ImGui::Text("GLAD reported vendor/renderer via glGetString.");
            }

            // --- Viewport / Window config ---
            if (ImGui::CollapsingHeader("Window / Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
                static int winW = 1280, winH = 720;
                ImGui::InputInt("Width", &winW);
                ImGui::InputInt("Height", &winH);
                if (ImGui::Button("Apply Window Size")) {
                    SDL_SetWindowSize(window, winW, winH);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Window Size")) {
                    SDL_SetWindowSize(window, 1280, 720);
                }

                ImGui::Checkbox("VSync", &g_vsync);
                if (ImGui::IsItemEdited()) {
                    SDL_GL_SetSwapInterval(g_vsync ? 1 : 0);
                }

                ImGui::InputInt("FPS Cap (0 = uncapped)", &g_fpsCap);
                if (g_fpsCap < 0) g_fpsCap = 0;

                ImGui::ColorEdit3("Viewport Clear Color", (float*)&g_clearColor);
                if (ImGui::Button("Apply Clear Color")) {
                    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
                    glClearColor(g_clearColor.x, g_clearColor.y, g_clearColor.z, g_clearColor.w);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                }

                ImGui::Separator();
                ImGui::Text("Culling");

                ImGui::Checkbox("Enable Frustum Culling", &g_enableFrustumCulling);

            }

            // --- Performance / graphs ---
            if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
                float avg = 0.0f;
                for (float t : g_frameTimes) avg += t;
                avg = g_frameTimes.empty() ? 0.0f : avg / (float)g_frameTimes.size();
                float fps = avg > 0.0f ? 1000.0f / avg : 0.0f;

                ImGui::Text("FPS (avg): %.1f", fps);
                ImGui::Text("Frame ms (avg): %.3f", avg);
                static std::vector<float> tmpBuf;
                tmpBuf.assign(g_frameTimes.begin(), g_frameTimes.end());
                if (tmpBuf.empty()) tmpBuf.assign(PERF_HISTORY, 0.0f);

                ImGui::PlotLines("Frame ms", tmpBuf.data(), (int)tmpBuf.size(), 0, nullptr, 0.0f, 50.0f, ImVec2(0, 80));
                ImGui::Text("Last frame time: %.3f ms", tmpBuf.empty() ? 0.0f : tmpBuf.back());
            }

            ImGui::End();
        }
        // === DIBUJAR GRID ===
        glUseProgram(gridShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
        glBindVertexArray(gridVAO);
        glDrawArrays(GL_LINES, 0, (GLsizei)(gridVertices.size() / 3));
        glBindVertexArray(0);

        sceneOctree.DebugDraw();

        // === DIBUJAR MODELOS ===
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glm::mat4 vp = projection * view;
        Frustum frustum = ExtractFrustum(vp);
       
        glm::vec3 frustumMin(-1000.0f);
        glm::vec3 frustumMax(1000.0f);

        std::vector<GameObject*> visibleObjects;
        sceneOctree.Query(frustumMin, frustumMax, visibleObjects);
        
        int drawnCount = 0;

        for (GameObject* goPtr : visibleObjects)
        {
            GameObject& go = *goPtr;
            if (go.hidden) continue;

            if (g_enableFrustumCulling)
            {
                if (!AABBInsideFrustum(frustum, go.worldAabbMin, go.worldAabbMax))
                    continue;
            }


            if (go.texComp.textureID != 0)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, go.texComp.textureID);
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, checkerTexture);
            }

            go.Draw(uModelLoc);
            drawnCount++;

            bool drawNormalsForThis = (go.normalsMode == GameObject::NM_ForceOn) ||
                (go.normalsMode == GameObject::NM_UseGlobal && showNormals);
            if (drawNormalsForThis)
            {
                glUseProgram(normalShaderProgram);
                glm::mat4 model = go.transform.GetMatrix();
                glUniformMatrix4fv(normalUModelLoc, 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix4fv(normalUViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(normalUProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

                // Offset proporcional al AABB local de la malla (no un valor fijo),
                // para que se vea razonable tanto en un modelo diminuto como en uno enorme.
                float localDiag = glm::length(go.aabbMax - go.aabbMin);
                glUniform1f(normalULengthLoc, localDiag * NORMAL_LENGTH_PERCENT);

                go.meshComp.mesh.DrawNormals();
                glUseProgram(shaderProgram);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        Console::DrawConsole("Console", &showConsole);

        // -------------------- Hierarchy panel (drag/rename/dup/delete) --------------------
        ImGui::Begin("Hierarchy");

        static int drag_source_idx = -1;
        static int rename_target_idx = -1;
        static char renameBuf[128] = { 0 };

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                if (selectedObject >= 0 && selectedObject < (int)gameObjects.size()) {
                    EraseGameObjectAt(gameObjects, selectedObject, selectedObject);
                }
            }
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
                if (selectedObject >= 0 && selectedObject < (int)gameObjects.size()) {
                    int newIdx = DuplicateGameObject(gameObjects, selectedObject);
                    if (newIdx != -1) selectedObject = newIdx;
                }
            }
        }

        for (int i = 0; i < (int)gameObjects.size(); ++i)
        {
            ImGui::PushID(i);

            ImVec4 textColor = gameObjects[i].hidden ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f) : ImVec4(1, 1, 1, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);

            bool isSelected = (selectedObject == i);
            if (ImGui::Selectable(gameObjects[i].name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedObject = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    rename_target_idx = i;
                    std::strncpy(renameBuf, gameObjects[i].name.c_str(), sizeof(renameBuf) - 1);
                    renameBuf[sizeof(renameBuf) - 1] = '\0';
                    ImGui::OpenPopup("Rename Popup");
                }
            }
            if (ImGui::BeginPopupContextItem(nullptr))
            {
                if (ImGui::MenuItem("Rename")) {
                    rename_target_idx = i;
                    std::strncpy(renameBuf, gameObjects[i].name.c_str(), sizeof(renameBuf) - 1);
                    renameBuf[sizeof(renameBuf) - 1] = '\0';
                    ImGui::OpenPopup("Rename Popup");
                }
                if (ImGui::MenuItem("Duplicate")) {
                    int newIdx = DuplicateGameObject(gameObjects, i);
                    if (newIdx != -1) {
                        selectedObject = newIdx;
                        Console::LogInfo("Duplicated %s -> %s", gameObjects[i].name.c_str(), gameObjects[newIdx].name.c_str());
                    }
                }
                if (ImGui::MenuItem("Delete")) {
                    EraseGameObjectAt(gameObjects, i, selectedObject);
                }
                ImGui::EndPopup();
            }

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                drag_source_idx = i; 
                ImGui::SetDragDropPayload("DND_GAMEOBJECT", &drag_source_idx, sizeof(int));
                ImGui::Text("%s", gameObjects[i].name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_GAMEOBJECT"))
                {
                    int src = *(const int*)payload->Data;
                    int dst = i;
                    if (src != dst && src >= 0 && src < (int)gameObjects.size())
                    {
                        GameObject tmp = std::move(gameObjects[src]);
                        if (src < dst) {
                            gameObjects.erase(gameObjects.begin() + src);
                            gameObjects.insert(gameObjects.begin() + dst, std::move(tmp));
                            if (selectedObject == src) selectedObject = dst;
                            else if (selectedObject > src && selectedObject <= dst) selectedObject--;
                        }
                        else {
                            gameObjects.erase(gameObjects.begin() + src);
                            gameObjects.insert(gameObjects.begin() + dst, std::move(tmp));
                            if (selectedObject == src) selectedObject = dst;
                            else if (selectedObject >= dst && selectedObject < src) selectedObject++;
                        }
                        sceneStructureDirty = true;
                        Console::LogInfo("Reordered: %d -> %d", src, dst);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        // Reconstruir el octree justo aqui (y no al final del frame): el panel Viewport, mas
        // abajo, hace picking consultando el octree en este mismo frame.
        if (sceneStructureDirty)
        {
            RebuildOctree();
            sceneStructureDirty = false;
        }

        if (ImGui::BeginPopupModal("Rename Popup", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (rename_target_idx >= 0 && rename_target_idx < (int)gameObjects.size())
            {
                ImGui::Text("Rename object:");
                ImGui::InputText("##rename", renameBuf, IM_ARRAYSIZE(renameBuf));
                ImGui::Separator();
                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    gameObjects[rename_target_idx].name = std::string(renameBuf);
                    ImGui::CloseCurrentPopup();
                    rename_target_idx = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                    rename_target_idx = -1;
                }
            }
            else
            {
                ImGui::Text("Target no valido");
                if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); rename_target_idx = -1; }
            }
            ImGui::EndPopup();
        }

        ImGui::End();


        ImGui::Begin("Viewport");
        ImGui::TextColored(
            g_enableFrustumCulling ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Frustum Culling: %s",
            g_enableFrustumCulling ? "ON" : "OFF"
        );

        ImGui::Text("Drawn objects: %d", drawnCount);
        ImGui::Separator();

        ImVec2 size = ImGui::GetContentRegionAvail();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImVec2 mousePos = ImGui::GetMousePos();

        relMouseX = mousePos.x - cursorPos.x;
        relMouseY = mousePos.y - cursorPos.y;
        mouseInsideViewport =
            (relMouseX >= 0 && relMouseX < size.x &&
                relMouseY >= 0 && relMouseY < size.y);

        if ((int)size.x != viewportWidth || (int)size.y != viewportHeight)
        {
            viewportWidth = (int)size.x;
            viewportHeight = (int)size.y;
            viewportWidthF = size.x;
            viewportHeightF = size.y;

            glBindTexture(GL_TEXTURE_2D, viewportTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, viewportWidth, viewportHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

            glBindRenderbuffer(GL_RENDERBUFFER, viewportRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth, viewportHeight);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, viewportRBO);
        }

        // ========================================================
        // CONTROL DE SELECCIÓN DE OBJETOS (RAYCAST) - VERSIÓN REVERT
        // ========================================================
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !altHeld && mouseInsideViewport && !ImGui::IsAnyItemHovered())
        {
            const float PICK_TOLERANCE_PIXELS = 3.0f;   // margen de click alrededor del AABB, en pixeles de pantalla
            const float pickPixelToWorld = 2.0f * std::tan(glm::radians(CAMERA_FOV_DEG) * 0.5f) / viewportHeightF; // mundo por pixel y por unidad de profundidad

            // relMouseX/Y ya son relativos al viewport (origen arriba-izq), tal como los espera la funcion
            glm::vec3 rayDir = ScreenToWorldRay_UnProject(
                relMouseX, relMouseY,
                viewportWidthF, viewportHeightF,
                projection, view);

            glm::vec3 rayOrigin = cameraPos;

            float closestDistance = std::numeric_limits<float>::max();
            int hitIndex = -1;

            glm::vec3 rayMin = rayOrigin + rayDir * 0.0f;
            glm::vec3 rayMax = rayOrigin + rayDir * 10000.0f;

            glm::vec3 rayAABBMin = glm::min(rayMin, rayMax);
            glm::vec3 rayAABBMax = glm::max(rayMin, rayMax);

            std::vector<GameObject*> candidates;
            sceneOctree.Query(rayAABBMin, rayAABBMax, candidates);

            Console::LogInfo("Octree candidates: %zu", candidates.size());

            for (GameObject* goPtr : candidates)
            {
                GameObject& go = *goPtr;
                int i = int(&go - &gameObjects[0]);
                if (go.hidden || go.meshComp.mesh.vertices.empty()) continue;

                float tHit = 0.0f;

                // Tolerancia de click: como maximo PICK_TOLERANCE_PIXELS pixeles alrededor del AABB. Un
                // pixel a profundidad d equivale a d * 2*tan(fov/2) / altoViewport unidades de mundo, asi el
                // margen es parecido en pantalla cerca o lejos (uno fijo en mundo no lo es). Se usa una cota
                // inferior de la profundidad de cualquier punto del AABB (la del centro menos la semidiagonal)
                // para que en sus caras mas cercanas, donde un pixel mide menos, el margen no se dispare.
                const glm::vec3 aabbCenter = (go.worldAabbMin + go.worldAabbMax) * 0.5f;
                const float halfDiag = 0.5f * glm::length(go.worldAabbMax - go.worldAabbMin);
                const float depth = std::max(glm::dot(aabbCenter - rayOrigin, cameraFront) - halfDiag, 0.1f);
                const float pickInflate = PICK_TOLERANCE_PIXELS * depth * pickPixelToWorld;

                glm::vec3 pickMin = go.worldAabbMin - glm::vec3(pickInflate);
                glm::vec3 pickMax = go.worldAabbMax + glm::vec3(pickInflate);

                if (!RayIntersectsAABB(rayOrigin, rayDir, pickMin, pickMax, tHit))
                    continue;


                glm::vec3 pointWorld = rayOrigin + rayDir * tHit;

                float worldDist = glm::length(pointWorld - rayOrigin);

                if (worldDist > 0.0f && worldDist < closestDistance)
                {
                    closestDistance = worldDist;
                    hitIndex = i;
                }

            } 

            if (hitIndex != -1)
            {
                selectedObject = hitIndex;
                std::cout << "Seleccionado: " << gameObjects[selectedObject].name << " (index=" << selectedObject << ")\n";
            }
            else
            {
                selectedObject = -1;
                std::cout << "Seleccion: ninguno\n";
            }
        }

        ImGui::Image((ImTextureID)(intptr_t)viewportTexture, size, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End(); // cierra Viewport

        // ============================================================
        // RENDERIZAR IMGUI (UNA VEZ POR FRAME)
        // ============================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
        }
        Uint64 now = SDL_GetPerformanceCounter();
        Uint64 freq = SDL_GetPerformanceFrequency();
        float deltaMs = (float)((now - g_lastFrameTick) * 1000.0 / (double)freq);
        g_lastFrameTick = now;

        g_frameTimes.push_back(deltaMs);
        if (g_frameTimes.size() > PERF_HISTORY) g_frameTimes.pop_front();

        if (g_fpsCap > 0) {
            float targetMs = 1000.0f / (float)g_fpsCap;
            if (deltaMs < targetMs) {
                Uint32 waitMs = (Uint32)std::ceil(targetMs - deltaMs);
                SDL_Delay(waitMs);
                Uint64 now2 = SDL_GetPerformanceCounter();
                deltaMs += (float)((now2 - g_lastFrameTick) * 1000.0 / (double)freq);
                g_lastFrameTick = now2;
                g_frameTimes.back() = deltaMs;
            }
        }
        SDL_GL_SwapWindow(window);       
    }
        // ========================================================
        // LIMPIEZA
        // ========================================================
        std::cout << " Saliendo del bucle principal." << std::endl;

        for (auto& go : gameObjects) go.Destroy();
        gameObjects.clear();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        glDeleteProgram(shaderProgram);
        glDeleteProgram(normalShaderProgram);
        glDeleteTextures(1, &checkerTexture);
        glDeleteFramebuffers(1, &viewportFBO);
        glDeleteTextures(1, &viewportTexture);
        glDeleteRenderbuffers(1, &viewportRBO);

        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
