#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "TextureLoader.h"

// Decodifica un .dds ya leido a memoria. Soporta DXT1/DXT3/DXT5 (se conservan comprimidos, con los
// mipmaps del archivo) y RGB/RGBA sin comprimir de 24/32 bits (se pasan a RGBA8). Los datos se
// voltean verticalmente para coincidir con las imagenes de stb_image (origen abajo a la izquierda),
// salvo con flipVertically = false (la opcion de importado flip Y deshace ese volteo).
// false si falla; el motivo se escribe en la Consola (pathForLog solo se usa en los mensajes).
bool DecodeDDS(const std::vector<uint8_t>& file, const std::string& pathForLog, TextureData& out, bool flipVertically = true);
