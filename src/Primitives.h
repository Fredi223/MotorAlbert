#pragma once

#include "ModuleModelLoader.h"

// Geometrias basicas generadas por codigo, con el mismo formato de vertice que los FBX
// (pos3, normal3, uv2, color3). Todas miden 1 unidad y estan centradas en el origen.
// Devuelven solo los datos de CPU (sin subir a la GPU): el ResourceManager las guarda en Library
// como cualquier otra malla y las sube al cargarlas.
Mesh CreateCubeMesh();
Mesh CreatePlaneMesh();
Mesh CreateSphereMesh(unsigned int sectors = 32, unsigned int stacks = 16);
Mesh CreateCylinderMesh(unsigned int segments = 32);
Mesh CreateConeMesh(unsigned int segments = 32);
