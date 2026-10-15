// sim/Player.hpp - the player character: position, movement, inventory and
// hand crafting.
#pragma once
#include "../core/Types.hpp"
#include "../core/Inventory.hpp"

class World;

enum class CraftMode : uint8_t { None, Single, Auto };
enum class CraftStatus : uint8_t { Ok, NoInputs, NoSpace };

struct Player {
    static constexpr int SIZE = CELL_SIZE - 4;   // 28 px square
    static constexpr float SPEED = 1.5f;
    static constexpr float FRICTION = 0.85f;
    static constexpr float WATER_SPEED = 0.7f;   // in water: 30% slower

    RectF rect{-SIZE / 2.0f, -SIZE / 2.0f, float(SIZE), float(SIZE)};
    float dx = 0, dy = 0;
    Inventory inventory{5, 9};

    // hand crafting
    int selectedRecipe = 0;
    CraftMode craftMode = CraftMode::None;
    int craftTicks = 0;            // progress of the current hand craft

    void setCenter(float x, float y) { rect.x = x - rect.w / 2; rect.y = y - rect.h / 2; }

    // One fixed tick of movement. inX/inY are -1, 0 or 1 from the keyboard.
    void update(World& world, int inX, int inY);

    // Is the given tile (partly) covered by the player?
    bool coversTile(Vec2i t) const;
};

// Hand crafting helpers (all-or-nothing, never lose items).
const Recipe* selectedHandRecipe(const Player& p);
CraftStatus craftStatus(const Inventory& inv, const Recipe* r);
bool tryCraft(Inventory& inv, const Recipe* r);
// One tick of the hand-crafting progress bar (only while the panel is open).
void updateHandCrafting(Player& p);
