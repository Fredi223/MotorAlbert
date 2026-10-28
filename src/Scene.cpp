#include "Scene.h"

#include <algorithm>
#include <cctype>

#include "Geometry.h"
#include "Logger.h"
#include "TextureLoader.h"

void Scene::RebuildOctree()
{
    glm::vec3 minB, maxB;

    if (objects.empty())
    {
        // ComputeSceneAABB no tiene objetos de los que sacar limites
        // reales (degeneraria a +-infinito). Usamos los mismos limites
        // fijos que ya se usan para la query de dibujado.
        minB = glm::vec3(-1000.0f);
        maxB = glm::vec3(1000.0f);
    }
    else
    {
        ComputeSceneAABB(objects, minB, maxB);
    }

    octree.Rebuild(minB, maxB);

    for (auto& go : objects)
    {
        go.UpdateWorldAABB();
        octree.Insert(&go);
    }

    Console::LogInfo("Octree rebuilt (%zu objects)", objects.size());
}

void Scene::LoadFbx(const std::string& filePath)
{
    float fbxUnitScale = 1.0f;
    std::vector<Mesh> newMeshes = LoadModel(filePath, &fbxUnitScale);

    const size_t lastSlash = filePath.find_last_of("/\\");
    const size_t nameStart = (lastSlash == std::string::npos) ? 0 : lastSlash + 1;
    const size_t lastDot = filePath.find_last_of('.');
    const std::string baseName = filePath.substr(nameStart,
        (lastDot == std::string::npos || lastDot < nameStart ? filePath.size() : lastDot) - nameStart);
    const std::string fbxDir = filePath.substr(0, nameStart);

    // BakerHouse.fbx es inconsistente: sus vertices ya vienen con Y arriba, pero su nodo
    // lleva ademas una rotacion de -90 en X (pensada para modelos con Z arriba, como el
    // Hotdog). Al hornearla la casa queda tumbada, con el tejado hacia -Z; +90 en X la
    // deja derecha. No hay regla general que distinga este caso, asi que se corrige por nombre.
    std::string lowerBase = baseName;
    std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(),
        [](unsigned char c) { return std::tolower(c); });
    const bool isBakerHouse = (lowerBase == "bakerhouse");

    const size_t currentSize = objects.size();
    for (size_t i = 0; i < newMeshes.size(); ++i)
    {
        GameObject go;
        go.name = baseName + "_" + std::to_string(i + currentSize);
        go.meshComp.mesh = std::move(newMeshes[i]);

        ComputeAABBFromMesh(go.meshComp.mesh, go.aabbMin, go.aabbMax);

        if (isBakerHouse) go.transform.rotation.x = 90.0f;
        go.transform.scale = glm::vec3(fbxUnitScale);

        go.texComp.textureID = defaultTexture;
        const std::string& storedPath = go.meshComp.mesh.texturePath;
        if (!storedPath.empty())
        {
            // La ruta del material puede ser solo el nombre de archivo o la ruta absoluta del
            // PC donde se exporto el FBX (que aqui no existe): se usa solo el nombre del archivo,
            // buscado junto al .fbx (no en el directorio de trabajo del proceso).
            const size_t texNameStart = storedPath.find_last_of("/\\");
            const std::string textureName = (texNameStart == std::string::npos) ? storedPath : storedPath.substr(texNameStart + 1);
            const std::string textureFullPath = fbxDir + textureName;

            if (GLuint tex = LoadImageTexture(textureFullPath))
            {
                go.texComp.textureID = tex;
                go.texComp.path = textureFullPath;
            }
            else
            {
                Console::LogError("Se usa la textura checker para %s", go.name.c_str());
            }
        }

        objects.push_back(std::move(go));
    }
    RebuildOctree();
    Console::LogInfo("FBX cargado: %s (%zu mallas -> GameObjects)", filePath.c_str(), newMeshes.size());
}

int Scene::AddPrimitive(const char* name, Mesh mesh)
{
    GameObject go;
    go.name = name;
    go.meshComp.mesh = std::move(mesh);
    ComputeAABBFromMesh(go.meshComp.mesh, go.aabbMin, go.aabbMax);
    go.texComp.textureID = defaultTexture;

    objects.push_back(std::move(go));
    RebuildOctree();
    Console::LogInfo("Geometria basica creada: %s", name);
    return static_cast<int>(objects.size()) - 1;
}

int Scene::Duplicate(int index)
{
    if (index < 0 || index >= (int)objects.size()) return -1;
    GameObject copy = objects[index];
    // La copia de Mesh duplica tambien los handles de GL (VAO/VBO/EBO): original y copia
    // apuntarian a los mismos buffers y borrar uno dejaria al otro sin geometria.
    // SetupMesh genera buffers propios a partir de vertices/indices, que si se copiaron.
    copy.meshComp.mesh.SetupMesh();
    copy.name = objects[index].name + "_copy";
    copy.transform.position += glm::vec3(0.1f, 0.0f, 0.1f);
    objects.insert(objects.begin() + index + 1, std::move(copy));
    structureDirty = true;
    return index + 1;
}

void Scene::Erase(int index, int& selected)
{
    if (index < 0 || index >= (int)objects.size()) return;
    const GLuint tex = objects[index].texComp.textureID;
    objects[index].Destroy();
    objects.erase(objects.begin() + index);
    structureDirty = true;
    ReleaseTextureIfUnused(tex);
    if (selected == index) {
        selected = -1;
    }
    else if (selected > index) {
        selected = selected - 1;
    }
}

void Scene::ReleaseTextureIfUnused(GLuint tex)
{
    if (tex == 0 || tex == defaultTexture) return;
    const bool inUse = std::any_of(objects.begin(), objects.end(),
        [tex](const GameObject& o) { return o.texComp.textureID == tex; })
        || (isTextureUsedElsewhere && isTextureUsedElsewhere(tex));
    if (!inUse) glDeleteTextures(1, &tex);
}
