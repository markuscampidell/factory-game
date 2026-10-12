// sim/Terrain.hpp - deterministic ore-patch generation.
// This is a 1:1 port of game/terrain.py: the same seed produces exactly the
// same ore patches as the Python version, so old saves keep their terrain.
#pragma once
#include "../core/Items.hpp"
#include <cstdint>

namespace terrain {

constexpr uint8_t EMPTY_TILE = 0;
// Tile type -> ore item. 1 iron, 2 copper, 3 coal, 4 limestone.
ItemId oreForTileType(uint8_t tileType);

uint8_t tileTypeAt(int64_t seed, int tileX, int tileY);

// Fills a CHUNK_SIZE*CHUNK_SIZE array (row-major) for chunk (cx, cy).
void generateChunk(int64_t seed, int cx, int cy, uint8_t* out);

void clearCache(); // call when switching worlds

} // namespace terrain
