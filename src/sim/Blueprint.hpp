// sim/Blueprint.hpp - a copied group of belts and machines ("map") that can
// be placed again somewhere else. Used for Ctrl+C / Ctrl+V and for saved
// blueprints (Ctrl+B / B).
#pragma once
#include "World.hpp"
#include <array>
#include <string>
#include <vector>

struct BlueprintBelt {
    Vec2i off;               // tile offset from the blueprint's top-left
    Dir dir = Dir::Right;
    ItemFilter filter;
    uint8_t mask = 0;        // incoming sides inside the blueprint (for drawing)
};

struct BlueprintMachine {
    MachineType type = MachineType::Smelter;
    Vec2i off;               // top-left tile offset
    Dir dir = Dir::Right;    // splitters
    std::string recipeId;    // smelters / assemblers ("" = default)
    std::array<ItemFilter, 3> sideFilters{}; // splitters
};

struct Blueprint {
    std::string name;
    int book = 0;            // id of the blueprint book it is in (0 = none)
    int w = 0, h = 0;        // size in tiles
    std::vector<BlueprintBelt> belts;
    std::vector<BlueprintMachine> machines;

    bool empty() const { return belts.empty() && machines.empty(); }
    int count() const { return int(belts.size() + machines.size()); }
    ItemAmounts cost() const;        // everything needed to build it
    void computeMasks();             // fills BlueprintBelt::mask
};

// A named group in the blueprint list. Books can't contain books.
struct BlueprintBook {
    int id = 0;              // > 0, unique; Blueprint::book refers to it
    std::string name;
};

// ---------------------------------------------------------------------------
// Selecting a rectangle (copy, blueprints, Ctrl+X delete) - shared rules
// ---------------------------------------------------------------------------
// Largest selection: 4 x 4 chunks.
constexpr int MAX_SELECTION = CHUNK_SIZE * 4;   // tiles per side

// The dragged rectangle (inclusive tiles). The end corner is pulled towards
// the start so it is never bigger than MAX_SELECTION x MAX_SELECTION.
struct Selection {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int w() const { return x1 - x0 + 1; }
    int h() const { return y1 - y0 + 1; }
};
Selection makeSelection(Vec2i start, Vec2i end);

// What a selection takes: every belt inside it, and every machine that is
// inside or partly inside. A machine that sticks out is taken whole - but
// only while everything taken still fits in MAX_SELECTION x MAX_SELECTION;
// otherwise it is left out (e.g. a machine touching a 128x128 selection
// with one or two tiles).
void gatherSelection(const World& w, const Selection& s, std::vector<int>& belts, std::vector<int>& machines);

// Everything a selection from `start` to `end` takes (see gatherSelection).
Blueprint captureBlueprint(const World& w, Vec2i start, Vec2i end);
