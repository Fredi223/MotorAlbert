#pragma once

#include "ModuleModelLoader.h"

// Geometrias basicas generadas por codigo, con el mismo formato de vertice que los FBX
// (pos3, normal3, uv2, color3). Todas miden 1 unidad y estan centradas en el origen.
// Necesitan un contexto OpenGL activo: devuelven la malla ya subida a la GPU (SetupMesh).
Mesh CreateCubeMesh();
Mesh CreatePlaneMesh();
Mesh CreateSphereMesh(unsigned int sectors = 32, unsigned int stacks = 16);
Mesh CreateCylinderMesh(unsigned int segments = 32);
Mesh CreateConeMesh(unsigned int segments = 32);
