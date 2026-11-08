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
    lineShaderProgram = CreateShaderProgram(lineVertexShaderSrc, lineFragmentShaderSrc);

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
    lineUViewProjLoc = glGetUniformLocation(lineShaderProgram, "uViewProj");

    // Lineas de debug: posicion (0) + color (1). El VBO se rellena en cada frame (FlushLines).
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

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
    glDeleteProgram(lineShaderProgram);
    glDeleteVertexArrays(1, &lineVAO);
    glDeleteBuffers(1, &lineVBO);
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

// ========================================================
// LINEAS DE DEBUG
// ========================================================
void Renderer::AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color)
{
    lineVertices.insert(lineVertices.end(), { a.x, a.y, a.z, color.r, color.g, color.b,
                                              b.x, b.y, b.z, color.r, color.g, color.b });
}

void Renderer::AddBox(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color)
{
    // Las 12 aristas: cada esquina i se une con las que difieren en un solo eje (bit).
    auto corner = [&](int i) {
        return glm::vec3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
    };
    for (int i = 0; i < 8; ++i)
        for (int bit = 1; bit < 8; bit <<= 1)
            if (!(i & bit)) AddLine(corner(i), corner(i | bit), color);
}

void Renderer::AddFrustum(const glm::mat4& vp, const glm::vec3& color)
{
    glm::vec3 c[8];
    FrustumCorners(vp, c);   // 0-3 plano cercano, 4-7 lejano
    for (int i = 0; i < 4; ++i)
    {
        AddLine(c[i], c[(i + 1) % 4], color);           // cercano
        AddLine(c[4 + i], c[4 + (i + 1) % 4], color);   // lejano
        AddLine(c[i], c[4 + i], color);                 // aristas laterales
    }
}

void Renderer::FlushLines(const glm::mat4& viewProj)
{
    if (lineVertices.empty()) return;
    glUseProgram(lineShaderProgram);
    glUniformMatrix4fv(lineUViewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(float), lineVertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, (GLsizei)(lineVertices.size() / 6));
    glBindVertexArray(0);
    lineVertices.clear();
}

RenderStats Renderer::Render(const Scene& scene, const glm::mat4& view, const glm::mat4& projection)
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

    // === FRUSTUM DEL CULLING ===
    // El de la culling camera si la escena tiene una; si no, el de la camara desde la que se mira.
    // Las camaras de juego usan el aspecto del viewport (no hay otra ventana de juego).
    const float aspect = (float)viewportWidth / (float)viewportHeight;
    RenderStats stats;
    glm::mat4 cullViewProj = projection * view;
    if (const GameObject* cullCam = scene.FindCullingCamera())
    {
        cullViewProj = cullCam->components.Get<ComponentCamera>()->GetProjection(aspect) * CameraViewFromWorld(cullCam->worldMatrix);
        stats.cullingCamera = cullCam->name;
    }
    const Frustum frustum = ExtractFrustum(cullViewProj);

    // El octree descarta de golpe los nodos fuera del frustum; a los objetos que quedan se les hace
    // el test fino de su propio AABB.
    std::vector<GameObject*> candidates;
    if (frustumCulling)
    {
        Octree::QueryStats q;
        scene.octree.QueryFrustum(frustum, candidates, &q);
        stats.nodesVisited = q.nodesVisited;
        stats.nodesTotal = q.nodesTotal;
    }
    else
    {
        scene.octree.QueryAll(candidates);
        stats.nodesTotal = scene.octree.NodeCount();
    }
    stats.candidates = (int)candidates.size();

    // === DIBUJAR MODELOS ===
    glUseProgram(shaderProgram);
    glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

    for (GameObject* goPtr : candidates)
    {
        const GameObject& go = *goPtr;
        const ComponentMesh* meshComp = go.ActiveMesh();   // el componente se puede haber desactivado este frame
        if (!meshComp || go.IsHiddenInHierarchy()) continue;

        if (frustumCulling && !AABBInsideFrustum(frustum, go.worldAabbMin, go.worldAabbMax))
            continue;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, go.ActiveTexture() != 0 ? go.ActiveTexture() : checkerTexture);

        go.Draw(uModelLoc);
        stats.drawn++;
        if (showAABBs) AddBox(go.worldAabbMin, go.worldAabbMax, glm::vec3(0.2f, 1.0f, 0.2f));

        bool drawNormalsForThis = (meshComp->normalsMode == ComponentMesh::NM_ForceOn) ||
            (meshComp->normalsMode == ComponentMesh::NM_UseGlobal && showNormals);
        const bool drawFaceNormalsForThis = meshComp->showFaceNormals || showFaceNormals;
        if (drawNormalsForThis || drawFaceNormalsForThis)
        {
            glUseProgram(normalShaderProgram);
            const glm::mat4& model = go.worldMatrix;
            glUniformMatrix4fv(normalUModelLoc, 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(normalUViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(normalUProjLoc, 1, GL_FALSE, glm::value_ptr(projection));

            // Offset proporcional al AABB local de la malla (no un valor fijo),
            // para que se vea razonable tanto en un modelo diminuto como en uno enorme.
            float localDiag = glm::length(meshComp->aabbMax - meshComp->aabbMin);
            glUniform1f(normalULengthLoc, localDiag * NORMAL_LENGTH_PERCENT);

            if (drawNormalsForThis)
            {
                glUniform3f(normalUColorLoc, 0.0f, 1.0f, 0.0f);
                meshComp->mesh->DrawNormals();
            }
            if (drawFaceNormalsForThis)
            {
                glUniform3f(normalUColorLoc, 1.0f, 0.2f, 1.0f);
                meshComp->mesh->DrawFaceNormals();
            }
            glUseProgram(shaderProgram);
        }
    }

    // Descartados = los que se verian (con malla y no ocultos) menos los dibujados.
    int visible = 0;
    scene.ForEach([&visible](const GameObject& go) { if (go.HasMesh() && !go.IsHiddenInHierarchy()) ++visible; });
    stats.culled = visible - stats.drawn;

    // === DEBUG DRAW ===
    if (showOctree)
        scene.octree.ForEachNode([this](const Octree::Node& n) {
            const bool occupiedLeaf = n.IsLeaf() && !n.objects.empty();
            AddBox(n.min, n.max, occupiedLeaf ? glm::vec3(0.3f, 0.6f, 1.0f) : glm::vec3(0.25f, 0.3f, 0.45f));
        });
    if (showCameraFrustums)
        scene.ForEach([&](const GameObject& go) {
            const auto* cam = go.components.Get<ComponentCamera>();
            if (!cam || !cam->enabled || go.IsHiddenInHierarchy()) return;
            const glm::vec3 color = cam->cullingCamera ? glm::vec3(1.0f, 0.5f, 0.0f) : glm::vec3(1.0f, 1.0f, 0.2f);
            AddFrustum(cam->GetProjection(aspect) * CameraViewFromWorld(go.worldMatrix), color);
        });
    if (showPickRay && hasPickRay)
        AddLine(pickRayFrom, pickRayTo, glm::vec3(1.0f, 0.2f, 0.2f));
    FlushLines(projection * view);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return stats;
}
