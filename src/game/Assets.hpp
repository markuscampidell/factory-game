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
const Texture2D* machineFlat(MachineType t);   // lying sprite, nullptr if there is none (yet)
// The right sprite for a machine in direction `d` over its footprint `dest`
// (splitters turned, a lying miner: the flat sprite or the standing one turned).
void drawMachine(MachineType t, Dir d, Rectangle dest, Color tint = WHITE);
// Belt sprite for a tile with these incoming travel directions (bitmask)
// and this outgoing direction - same rules as BeltSegmentSpriteManager.
const Texture2D& belt(uint8_t incomingMask, Dir out);
void drawPipe(uint8_t connections, Rectangle dest, Color tint = WHITE);   // bit i = connected on side Dir(i)

// Fonts are created on demand per pixel size.
const Font& font(int size, bool bold = false);

} // namespace assets
