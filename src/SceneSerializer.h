#pragma once
#include <filesystem>
#include <nlohmann/json_fwd.hpp>

class ResourceManager;
class Scene;

// Guarda y carga la escena en JSON (archivos .scene en Assets/Scenes/).
//
// Formato: { "version": 1, "gameObjects": [ ... ] } con los objetos en preorden (cada padre antes que
// sus hijos). Cada objeto lleva su UID y el de su padre (0 = primer nivel), nombre, hidden, transform
// LOCAL y sus componentes. Las mallas y texturas se guardan por el UID de su recurso: al cargar se
// piden al ResourceManager, que las carga de Library/ si nadie las estaba usando. Nunca se guardan punteros.
//
// Lo usan File > Save/Open Scene y, en la Fase 6, Play/Stop (guardar la escena en memoria y restaurarla).
namespace SceneSerializer
{
    nlohmann::json ToJson(const Scene& scene);

    // Sustituye el contenido de la escena por el del JSON y reconstruye el octree. Si el JSON no es una
    // escena valida devuelve false y la escena queda como estaba. Un recurso que ya no existe deja el
    // componente sin malla o sin textura (con un aviso en la consola), y no impide cargar el resto.
    bool FromJson(const nlohmann::json& data, Scene& scene, ResourceManager& resources);

    bool SaveToFile(const Scene& scene, const std::filesystem::path& file);
    bool LoadFromFile(const std::filesystem::path& file, Scene& scene, ResourceManager& resources);
}
