#include "Shaders.h"
#include "Logger.h"

// ============================================================
// SHADERS BÁSICOS
// ============================================================
const char* vertexShaderSource = R"(
#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aColor;

out vec3 Normal;
out vec3 FragPos;
out vec3 Color;
out vec2 TexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    FragPos = vec3(uModel * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(uModel))) * aNormal;
    Color = aColor;
    TexCoord = aTexCoord;
    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 460 core
in vec3 Normal;
in vec3 Color;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    vec3 light = normalize(vec3(0.3, 0.5, 0.8));
    float diff = max(dot(normalize(Normal), light), 0.0);

    vec3 texColor = texture(uTexture, TexCoord).rgb;
    vec3 finalColor = texColor * (0.4 + diff * 0.6);   // luz ambiente 0.4 + difusa

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
uniform vec3 uNormalColor;   // verde: normales por vertice; magenta: normales por cara
void main()
{
    FragColor = vec4(uNormalColor, 1.0);
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

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        Console::LogError("Error compilando shader: %s", info);
    }
    return shader;
}

// geometrySrc es opcional (solo lo usa el shader de normales).
GLuint CreateShaderProgram(const char* vertexSrc, const char* fragmentSrc, const char* geometrySrc)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    GLuint gs = geometrySrc ? CompileShader(GL_GEOMETRY_SHADER, geometrySrc) : 0;
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    if (gs) glAttachShader(program, gs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success)
    {
        char info[512];
        glGetProgramInfoLog(program, 512, nullptr, info);
        Console::LogError("Error linkeando shader program: %s", info);
    }

    glDeleteShader(vs);
    if (gs) glDeleteShader(gs);
    glDeleteShader(fs);

    return program;
}
