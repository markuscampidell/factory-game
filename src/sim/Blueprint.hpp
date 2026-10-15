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
    bool bridge = false;     // on the bridge layer
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
    std::vector<Vec2i> landfill;     // landfilled tiles (offsets); filled again where there is water

    bool empty() const { return belts.empty() && machines.empty() && landfill.empty(); }
    int count() const { return int(belts.size() + machines.size() + landfill.size()); }
    ItemAmounts cost() const;        // everything needed to build it (landfill: as if every tile were water)
    void computeMasks();             // fills BlueprintBelt::mask
};

// Turned / mirrored copies (R / T / G while holding a blueprint or copy).
// Belts, splitters and miners turn with it (a miner lies down / stands up);
// a mirrored splitter swaps its left and right filters.
Blueprint rotatedCW(const Blueprint& bp, int quarterTurns = 1);
Blueprint mirrored(const Blueprint& bp);   // left <-> right

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

// Layers you work on (L in the game): the ground (belts, splitters,
// machines, landfill), the bridges on top, or both.
enum class Layers : uint8_t { Ground = 1, Bridges = 2, All = 3 };
inline bool hasGround(Layers l) { return (uint8_t(l) & 1) != 0; }
inline bool hasBridges(Layers l) { return (uint8_t(l) & 2) != 0; }

// The bridges inside a selection (belt ids).
std::vector<int> gatherBridges(const World& w, const Selection& s);

// The landfilled tiles inside a selection (row by row).
std::vector<Vec2i> gatherLandfill(const World& w, const Selection& s);

// Which things a selection takes:
//  * Everything: belts, bridges, machines and landfill (blueprints, Ctrl + B).
//  * ObjectsFirst: belts, bridges and machines if there are any - otherwise
//    only the landfill, and only tiles with nothing on them (Copy area /
//    Delete area: dragging over a factory on landfill takes just the factory).
// Only from the given layers (landfill belongs to the ground).
enum class SelectRule { Everything, ObjectsFirst };
struct SelectionContent {
    std::vector<int> belts, machines;   // (ground belts)
    std::vector<int> bridges;           // belt ids on the bridge layer
    std::vector<Vec2i> landfill;
    bool empty() const { return belts.empty() && machines.empty() && bridges.empty() && landfill.empty(); }
};
SelectionContent gatherContent(const World& w, const Selection& s, SelectRule rule, Layers layers = Layers::All);

// What a selection from `start` to `end` takes (see gatherContent).
Blueprint captureBlueprint(const World& w, Vec2i start, Vec2i end, SelectRule rule = SelectRule::Everything, Layers layers = Layers::All);

// The same blueprint without its landfill ("Include landfill" off): size and
// offsets shrink to the belts and machines. Empty if there is nothing else.
Blueprint withoutLandfill(const Blueprint& bp);
