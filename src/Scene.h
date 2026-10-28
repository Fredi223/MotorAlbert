#pragma once
#include <functional>
#include <string>
#include <vector>
#include <glad/glad.h>

#include "GameObject.h"
#include "ModuleModelLoader.h"
#include "Octree.h"

// Escena del motor: los GameObject y el octree que los indexa.
//
// El octree guarda GameObject* directos a los elementos de objects (std::vector por valor).
// Cualquier insert/erase/push_back invalida esos punteros (reasigna el buffer o desplaza y
// destruye elementos), asi que toda mutacion estructural de objects debe marcar structureDirty;
// el bucle principal llama a RebuildOctree antes de volver a usar el octree.
class Scene
{
public:
    std::vector<GameObject> objects;
    Octree octree{ glm::vec3(-1000.0f), glm::vec3(1000.0f) };
    bool structureDirty = false;

    // Textura que reciben los objetos nuevos (la checker). Es global: nunca se libera desde aqui.
    GLuint defaultTexture = 0;

    // Pregunta si alguien fuera de la escena (p. ej. el historial de undo) sigue usando una
    // textura. La escena no sabe quien es; si no hay nadie conectado, solo cuentan sus objetos.
    std::function<bool(GLuint)> isTextureUsedElsewhere;

    // Recalcula los limites del octree a partir de los objetos y los vuelve a insertar.
    void RebuildOctree();

    // Carga un .fbx y crea un GameObject por malla (sin jerarquia), con su textura si la encuentra.
    void LoadFbx(const std::string& filePath);

    // Crea un GameObject con una geometria basica y la textura por defecto. Devuelve su indice.
    int AddPrimitive(const char* name, Mesh mesh);

    // Copia el objeto justo detras del original, con buffers de GPU propios. Devuelve el indice
    // de la copia, o -1 si index no es valido.
    int Duplicate(int index);

    // Borra un objeto y corrige el indice de seleccion (-1 si era el borrado).
    void Erase(int index, int& selected);

    // Una textura puede estar compartida entre varios objetos (duplicados): solo se libera cuando
    // ningun GameObject ni nadie de fuera (isTextureUsedElsewhere) la usa ya, y nunca la de por
    // defecto. Hay que llamarla DESPUES de haber quitado la textura del objeto que la dejaba, o ese
    // objeto la seguiria contando.
    void ReleaseTextureIfUnused(GLuint tex);
};
