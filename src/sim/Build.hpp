// sim/Build.hpp - placing and deleting belts and machines.
//
// Item conservation rules (the whole point of this module):
//  * Every action is simulated on a copy of the player's inventory first.
//    Only if the copy succeeds completely is it done for real.
//  * Deleting/replacing anything refunds its build cost AND everything it
//    holds. If the refund doesn't fit in the inventory, nothing happens.
//  * Building charges the full cost or nothing.
#pragma once
#include "World.hpp"
#include "Player.hpp"
#include "Blueprint.hpp"
#include <array>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

// Undiscovered: (part of) it would be in a chunk you haven't explored yet.
// OutOfReach: (part of) a blueprint / copy would be outside the chunks
// around the player (Builder::blueprintReach).
// OnWater: something would stand on water; NeedsWater: a water pump that
// doesn't touch water.
// NoWater: landfill where there is no water to fill.
// TooLong: more than BRIDGE_MAX_LENGTH bridge tiles in a row.
// Unconnected: a bridge whose first tile nothing points straight into (no
// belt, splitter or machine behind it) - bridges can't float in the air.
// FluidMix: a pipe would connect pipes / pumps holding different fluids.
enum class PlaceStatus : uint8_t { Ok, Blocked, NoSpace, NoFunds, NoOre, Undiscovered, OutOfReach, OnWater, NeedsWater, NoWater, TooLong, Unconnected, FluidMix };

// Landfill turns water into land: 10 limestone per water tile. Placed as a
// square of LANDFILL_MIN..LANDFILL_MAX tiles per side (+ / - or Shift + mouse
// wheel while holding it); only the water tiles in it are filled and paid.
constexpr int LANDFILL_MIN = 1, LANDFILL_MAX = 10, LANDFILL_DEFAULT = 2;
const ItemAmounts& landfillTileCost();

// Water pumps must have water right next to them (one of the tiles around
// their footprint, not diagonally).
bool touchesWater(World& w, MachineType t, Vec2i topLeft, Dir d);
bool pipeWouldMixFluids(const World& w, Vec2i tile);   // see PlaceStatus::FluidMix

const ItemAmounts& beltBuildCost();
const ItemAmounts& bridgeBuildCost();   // double a belt
inline const ItemAmounts& beltCost(bool bridge) { return bridge ? bridgeBuildCost() : beltBuildCost(); }

struct BeltPlan {
    Vec2i pos;
    Dir dir;
    std::optional<Dir> newIncoming; // flow direction arriving from the drag
};

// One thing to place - used by every kind of placement (single machine, a
// line of belts, a blueprint) so they all follow the same rules:
//  * An identical object already there (belt: same tile + direction;
//    machine: same type + position, splitter also same direction) is kept
//    and just updated - it costs nothing and gives nothing back.
//  * Anything else in the way blocks, unless replacing (Shift):
//      - belt over belt / machine over the same type at the same position:
//        kept and updated in place (direction, recipe, filters). A belt keeps
//        its item, a machine keeps its contents - they only go to the player
//        if its recipe actually changes.
//      - everything else is removed and refunded (build cost + contents).
//  * New objects cost their build cost. All or nothing.
struct PlaceRequest {
    bool belt = false;
    bool bridge = false;              // a belt on the bridge layer (anywhere but on machines / splitters)
    Vec2i pos;                        // belt tile / machine top-left
    MachineType type = MachineType::Smelter;
    Dir dir = Dir::Right;
    std::string recipeId;             // "" = keep (existing) / default (new)
    bool setFilters = false;          // blueprints carry filters
    ItemFilter beltFilter;
    std::array<ItemFilter, 3> sideFilters{};
};

struct PlacePlan {
    PlaceStatus status = PlaceStatus::Ok;
    std::vector<PlaceStatus> each;    // per request: Ok / Blocked / NoOre
    std::vector<int> existing;        // per request: object kept in place (belt / machine id) or -1
    std::vector<int> removeBelts, removeMachines;
    ItemAmounts delta;                // inventory change: + got back, - paid
    ItemAmounts cost;                 // build cost of the new objects only (+ landfill)
    ItemAmounts refund;               // what comes back: replaced things (build cost + contents), recipe changes
    std::vector<Vec2i> fill;          // water tiles filled first (blueprints with landfill)
};

class Builder {
public:
    Builder(World& w, Player& p) : world(w), player(p) {}

    // ---- shared ----
    bool isBlockedByPlayer(Vec2i t) const { return player.coversTile(t); }
    // The game only lets you build in explored chunks (the headless tests
    // leave this off).
    bool requireExplored = false;
    bool tileExplored(Vec2i t) const { return world.isExplored(floorDiv(t.x, CHUNK_SIZE), floorDiv(t.y, CHUNK_SIZE)); }
    // Blueprints and copies can only be placed in the chunk the player
    // stands in and `blueprintReach` chunks around it (the game uses 4, so
    // 9 x 9 chunks; -1 = anywhere, the headless tests leave it off).
    int blueprintReach = -1;
    Vec2i playerChunk() const;
    // The allowed area in tiles: x0..x1, y0..y1 inclusive (only if reach >= 0)
    void reachTiles(int& x0, int& y0, int& x1, int& y1) const;
    bool inBlueprintReach(Vec2i t) const;
    void gatherOccupants(const std::vector<Vec2i>& cells, std::vector<int>& belts, std::vector<int>& machines) const;
    bool applyRefunds(Inventory& inv, const std::vector<int>& belts, const std::vector<int>& machines) const;
    // Exactly what applyRefunds() gives back: build cost + contents.
    ItemAmounts refundTotals(const std::vector<int>& belts, const std::vector<int>& machines) const;

    // Actions below can report how the player's inventory changed in `delta`
    // (positive = got back, negative = paid), e.g. for "+1 Iron Ingot" texts.

    // ---- placing anything (see PlaceRequest) ----
    // limitReach: everything must be inside the blueprint reach (blueprints / copies)
    // `fill`: landfill tiles (blueprints) - those that are water now are
    // filled first and count as land for everything placed with them.
    PlacePlan planPlacement(const std::vector<PlaceRequest>& reqs, bool allowReplace,
                            const std::set<int>* protectedIds = nullptr, bool limitReach = false,
                            const std::vector<Vec2i>* fill = nullptr) const;
    // Does it (plan.status must be Ok). Returns the belt / machine id per request.
    std::vector<int> executePlacement(const std::vector<PlaceRequest>& reqs, const PlacePlan& plan);

    // ---- machines ----
    static Vec2i machineTopLeft(MachineType t, Vec2i mouseTile, Dir d = Dir::Right);
    // planOut: the whole plan - removeBelts / removeMachines are what the
    // placement takes away (replaced, or bridges that would float) - for
    // marking them red
    PlaceStatus checkMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, ItemAmounts* cost = nullptr,
                             ItemAmounts* refund = nullptr, PlacePlan* planOut = nullptr) const;
    std::vector<int> bridgesAmong(const std::vector<int>& beltIds) const;
    // Returns the new machine id, or -1. `protectedIds` are never replaced.
    int placeMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, const std::set<int>* protectedIds,
                     ItemAmounts* delta = nullptr);
    bool canAffordMachineDeletion(int id) const;
    // Bridges can't float: deleting what feeds a bridge (the belt, splitter
    // or machine pointing into its first tile) or a bridge tile with more of
    // the bridge in front of it takes those bridge tiles along. The bridge
    // ids (not in `belts`) that deleting `belts` + `machines` would take along:
    std::vector<int> dependentBridges(const std::vector<int>& belts, const std::vector<int>& machines) const;
    // Can all of it (with its dependent bridges) be refunded?
    bool canAffordDeletion(const std::vector<int>& belts, const std::vector<int>& machines) const;
    // Deletes it with its dependent bridges - all or nothing (inventory space).
    bool deleteObjects(std::vector<int> belts, const std::vector<int>& machines, ItemAmounts* delta = nullptr);
    // whole: a pipe takes its whole pipe system (every connected pipe) along
    bool deleteMachineAt(Vec2i t, ItemAmounts* delta = nullptr, bool whole = false);
    std::vector<int> connectedPipes(int pipeId) const;   // the pipe system of a pipe (machine ids)

    // Switching a machine's recipe hands its contents back to the player.
    // Refuses (returns false) if they don't fit.
    bool setRecipe(Machine& m, const Recipe* r);

    // Ctrl + left click: moves as much as fits into the player's inventory -
    // a machine's outputs, or everything in a storage box. `got` = what was
    // moved. Nothing: there was nothing to take; NoSpace: none of it fits.
    enum class TakeResult { Ok, Nothing, NoSpace };
    TakeResult takeContents(Machine& m, ItemAmounts* got = nullptr);
    // E: picks up the items lying on the belts and splitters the player
    // stands on (as far as they fit). Nothing: none there; NoSpace: none fit.
    TakeResult pickUpUnderPlayer(ItemAmounts* got = nullptr);

    // ---- landfill ----
    static Vec2i landfillTopLeft(Vec2i mouseTile, int size) { return {mouseTile.x - size / 2, mouseTile.y - size / 2}; }
    struct LandfillCheck {
        PlaceStatus status = PlaceStatus::Ok;   // Ok, NoWater, NoFunds, Undiscovered
        std::vector<Vec2i> tiles;               // the water tiles that would be filled
        ItemAmounts cost;
    };
    LandfillCheck checkLandfill(Vec2i topLeft, int size) const;
    bool placeLandfill(Vec2i topLeft, int size, ItemAmounts* delta = nullptr);
    // Deleting landfill turns it back into water and gives the limestone
    // back (10 per tile) - only where nothing stands on it, and only if all
    // of it fits into the inventory. Recorded for undo.
    bool canDeleteLandfillAt(Vec2i t) const { return world.isLandfill(t) && !world.isCellBlocked(t); }
    bool canAffordLandfillDeletion(int tiles) const;
    bool deleteLandfillAt(Vec2i t, ItemAmounts* delta = nullptr);

    // R when not building: turns the belt, splitter, smelter or assembler at
    // tile t by `steps` quarter turns clockwise. Everything inside stays
    // (items, filters, recipe); recorded for undo. False: nothing turnable,
    // or (`noSpace`) a bridge it fed would float and its refund doesn't fit -
    // otherwise that bridge is deleted with it (refund in `got`).
    bool turnAt(Vec2i t, int steps, bool* noSpace = nullptr, ItemAmounts* got = nullptr);
    // Deletes the bridges that are floating now but were supported before
    // (`supportedBefore`: positions) - all or nothing; false: no space.
    bool removeNewlyFloating(const std::unordered_set<Vec2i, Vec2iHash>& supportedBefore, ItemAmounts* got, bool dryRun = false);
    std::unordered_set<Vec2i, Vec2iHash> supportedBridges() const;

    // ---- bridges (see Belt in World.hpp) ----
    // Always straight: from `start` towards `end` along the longer axis, at
    // most BRIDGE_MAX_LENGTH tiles; a single tile points `dir`.
    static std::vector<BeltPlan> planBridge(Vec2i start, Vec2i end, Dir dir);
    PlacePlan checkBridge(const std::vector<BeltPlan>& plan, bool allowReplace) const;
    bool placeBridge(const std::vector<BeltPlan>& plan, bool allowReplace, ItemAmounts* delta = nullptr);
    // the bridge tiles in a row with bridge `id` (same direction)
    std::vector<int> bridgeRun(int id) const;
    bool deleteBridgeAt(Vec2i t, bool whole, ItemAmounts* delta = nullptr);

    // ---- belts ----
    std::vector<Vec2i> dragTiles(Vec2i start, Vec2i end, Dir placementDir) const;
    std::vector<BeltPlan> planBelts(Vec2i start, Vec2i end, Dir placementDir) const;
    bool othersFreeOrMatching(const std::vector<BeltPlan>& plan, Vec2i start) const;
    // A new belt would go on water there (a belt already there may be turned in place).
    bool beltTileOnWater(Vec2i t) const { return world.beltIdAt(t) < 0 && const_cast<World&>(world).isWater(t); }
    bool anyBeltTileOnWater(const std::vector<BeltPlan>& plan) const;
    bool isDragTileBlocked(const BeltPlan& p, Vec2i start, bool othersOk, bool allowReplace) const;
    bool anyDragTileBlocked(const std::vector<BeltPlan>& plan, Vec2i start, bool allowReplace) const;
    PlaceStatus beltAffordability(const std::vector<BeltPlan>& plan, ItemAmounts* cost = nullptr, ItemAmounts* refund = nullptr,
                                  PlacePlan* planOut = nullptr) const;
    // Per tile "can afford so far" flags for the drag preview.
    std::vector<bool> runningAffordability(const std::vector<BeltPlan>& plan) const;
    bool placeBelts(Vec2i start, Vec2i end, Dir placementDir, bool allowReplace, ItemAmounts* delta = nullptr);

    // ---- blueprints (copy / paste of many things at once) ----
    // Where the blueprint's top-left goes when the mouse is on `mouseTile`.
    static Vec2i blueprintTopLeft(const Blueprint& bp, Vec2i mouseTile) { return {mouseTile.x - bp.w / 2, mouseTile.y - bp.h / 2}; }
    struct BlueprintCheck {
        PlaceStatus status = PlaceStatus::Ok;
        std::vector<PlaceStatus> belts, machines;   // per entity: Ok / Blocked / NoOre
        ItemAmounts cost;                           // only what is new
        ItemAmounts refund;                         // what replacing gives back
        std::vector<int> removeBelts, removeMachines;   // what it takes away (see checkMachine)
    };
    BlueprintCheck checkBlueprint(const Blueprint& bp, Vec2i topLeft, bool allowReplace = false) const;
    // All or nothing: needs free tiles (or identical objects, or Shift),
    // ore under new miners and the full cost.
    bool placeBlueprint(const Blueprint& bp, Vec2i topLeft, bool allowReplace = false, ItemAmounts* delta = nullptr);

    // ---- undo (Ctrl+Z) ----
    // Takes back one recorded action (World::recordChanges): removes what it
    // added (refunding them with their contents) and puts back what it
    // removed or changed, with the contents they had (paying for them).
    // All or nothing - never creates or loses items.
    // Floating: a bridge it brings back would be left in the air. Bridges it
    // leaves in the air (their belt / machine goes) go too, refunded.
    enum class UndoResult { Ok, Nothing, Changed, Blocked, PlayerInTheWay, NoFunds, NoSpace, Floating };
    // `inverse` gets the record that takes this undo back again (redo).
    UndoResult undo(const UndoRecord& rec, ItemAmounts* delta = nullptr, UndoRecord* inverse = nullptr);

    // ---- deleting everything in a rectangle (Ctrl+X) ----
    enum class AreaDelete { Ok, Nothing, NoSpace };
    // All or nothing: what the selection takes (gatherContent with
    // SelectRule::ObjectsFirst, max MAX_SELECTION x MAX_SELECTION) is removed and refunded -
    // the belts and machines (build cost plus contents) if there are any,
    // otherwise the landfill (10 limestone per tile) - but only if all of it
    // fits into the inventory. `refunded` gets the items, `removed` counts
    // objects or landfill tiles.
    // Only on `layers` (gatherContent with SelectRule::ObjectsFirst).
    AreaDelete deleteArea(Vec2i a, Vec2i b, ItemAmounts* refunded = nullptr, int* removed = nullptr, Layers layers = Layers::Ground);

    std::vector<int> connectedBelts(int beltId) const;
    bool canAffordBeltDeletion(const std::vector<int>& ids) const;
    bool deleteBeltAt(Vec2i t, bool whole, ItemAmounts* delta = nullptr);

    World& world;
    Player& player;

private:
    bool segmentMatchesExisting(const BeltPlan& p) const;
    bool segmentReversesExisting(const BeltPlan& p) const;
    bool tileAutoReplaceable(const BeltPlan& p) const;
};
