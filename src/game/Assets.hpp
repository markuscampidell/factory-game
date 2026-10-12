// game/Assets.hpp - textures and fonts.
#pragma once
#include "raylib.h"
#include "../core/Items.hpp"
#include "../sim/World.hpp"
#include <string>

namespace assets {

void load();
void unload();

// nullptr if the item has no sprite
const Texture2D* item(ItemId id);
const Texture2D* machine(MachineType t);
// Belt sprite for a tile with these incoming travel directions (bitmask)
// and this outgoing direction - same rules as BeltSegmentSpriteManager.
const Texture2D& belt(uint8_t incomingMask, Dir out);

// Fonts are created on demand per pixel size.
const Font& font(int size, bool bold = false);

} // namespace assets
