#include "Renderer.h"

#include <vector>
#include <glm/gtc/type_ptr.hpp>

#include "Geometry.h"
#include "Logger.h"
#include "Scene.h"
#include "Shaders.h"

void Renderer::Init(int width, int height)
{
    shaderProgram = CreateShaderProgram(vertexShaderSource, fragmentShaderSource);
    normalShaderProgram = CreateShaderProgram(normalVertexShaderSrc, normalFragmentShaderSrc, normalGeometryShaderSrc);
    gridShaderProgram = CreateShaderProgram(gridVertexShaderSrc, gridFragmentShaderSrc);

    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture"), 0);

    uModelLoc = glGetUniformLocation(shaderProgram, "uModel");
    uViewLoc = glGetUniformLocation(shaderProgram, "uView");
    uProjLoc = glGetUniformLocation(shaderProgram, "uProjection");

    normalUModelLoc = glGetUniformLocation(normalShaderProgram, "uModel");
    normalUViewLoc = glGetUniformLocation(normalShaderProgram, "uView");
    normalUProjLoc = glGetUniformLocation(normalShaderProgram, "uProjection");
    normalULengthLoc = glGetUniformLocation(normalShaderProgram, "uNormalLength");
    normalUColorLoc = glGetUniformLocation(normalShaderProgram, "uNormalColor");

    gridUViewLoc = glGetUniformLocation(gridShaderProgram, "uView");
    gridUProjLoc = glGetUniformLocation(gridShaderProgram, "uProjection");

    viewportWidth = width;
    viewportHeight = height;
    CreateViewportFramebuffer();
    CreateCheckerTexture();
    CreateGrid();
}

void Renderer::Shutdown()
{
    glDeleteProgram(shaderProgram);
    glDeleteProgram(normalShaderProgram);
    glDeleteProgram(gridShaderProgram);
    glDeleteVertexArrays(1, &gridVAO);
    glDeleteBuffers(1, &gridVBO);
    glDeleteTextures(1, &checkerTexture);
    glDeleteFramebuffers(1, &viewportFBO);
    glDeleteTextures(1, &viewportTexture);
    glDeleteRenderbuffers(1, &viewportRBO);
}

// ============================================================
// FRAMEBUFFER PARA EL VIEWPORT DEL EDITOR
// ============================================================
void Renderer::CreateViewportFramebuffer()
{
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
        Console::LogError("El framebuffer del viewport no esta completo");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::ResizeViewport(int width, int height)
{
    viewportWidth = width;
    viewportHeight = height;

    glBindTexture(GL_TEXTURE_2D, viewportTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, viewportWidth, viewportHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    // El attachment se hace con el FBO del viewport enlazado: sobre el framebuffer por defecto (0)
    // glFramebufferRenderbuffer es un error de OpenGL.
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, viewportRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, viewportWidth, viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, viewportRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================
// CREAR TEXTURA CHECKERBOARD
// ============================================================
void Renderer::CreateCheckerTexture()
{
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
}

// ========================================================
// CREAR GRID (REJILLA DE REFERENCIA)
// ========================================================
void Renderer::CreateGrid()
{
    std::vector<float> gridVertices;

    int gridSize = 20;
    float spacing = 1.0f;

    for (int i = -gridSize; i <= gridSize; ++i)
    {
        // Líneas paralelas al eje Z (x = i)
        gridVertices.push_back((float)i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)-gridSize * spacing);

        gridVertices.push_back((float)i * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)gridSize * spacing);

        // Líneas paralelas al eje X (z = i)
        gridVertices.push_back((float)-gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)i * spacing);

        gridVertices.push_back((float)gridSize * spacing);
        gridVertices.push_back(0.0f);
        gridVertices.push_back((float)i * spacing);
    }
    gridVertexCount = (GLsizei)(gridVertices.size() / 3);

    glGenVertexArrays(1, &gridVAO);
    glGenBuffers(1, &gridVBO);
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(float), gridVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

int Renderer::Render(const Scene& scene, const glm::mat4& view, const glm::mat4& projection)
{
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // === DIBUJAR GRID ===
    glUseProgram(gridShaderProgram);
    glUniformMatrix4fv(gridUViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(gridUProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glBindVertexArray(gridVAO);
    glDrawArrays(GL_LINES, 0, gridVertexCount);
    glBindVertexArray(0);

    // === DIBUJAR MODELOS ===
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

    glm::mat4 vp = projection * view;
    Frustum frustum = ExtractFrustum(vp);

    // El octree se consulta con una caja fija que cubre toda la escena; el culling real es
    // el test del AABB de cada objeto contra el frustum.
    std::vector<GameObject*> visibleObjects;
    scene.octree.Query(glm::vec3(-1000.0f), glm::vec3(1000.0f), visibleObjects);

    int drawnCount = 0;

    for (GameObject* goPtr : visibleObjects)
    {
        GameObject& go = *goPtr;
        if (go.hidden) continue;

        if (frustumCulling)
        {
            if (!AABBInsideFrustum(frustum, go.worldAabbMin, go.worldAabbMax))
                continue;
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, go.texComp.textureID != 0 ? go.texComp.textureID : checkerTexture);

        go.Draw(uModelLoc);
        drawnCount++;

        bool drawNormalsForThis = (go.normalsMode == GameObject::NM_ForceOn) ||
            (go.normalsMode == GameObject::NM_UseGlobal && showNormals);
        const bool drawFaceNormalsForThis = go.showFaceNormals || showFaceNormals;
        if (drawNormalsForThis || drawFaceNormalsForThis)
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

            if (drawNormalsForThis)
            {
                glUniform3f(normalUColorLoc, 0.0f, 1.0f, 0.0f);
                go.meshComp.mesh.DrawNormals();
            }
            if (drawFaceNormalsForThis)
            {
                glUniform3f(normalUColorLoc, 1.0f, 0.2f, 1.0f);
                go.meshComp.mesh.DrawFaceNormals();
            }
            glUseProgram(shaderProgram);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return drawnCount;
}
