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
#include <vector>

// Undiscovered: (part of) it would be in a chunk you haven't explored yet.
// OutOfReach: (part of) a blueprint / copy would be outside the chunks
// around the player (Builder::blueprintReach).
enum class PlaceStatus : uint8_t { Ok, Blocked, NoSpace, NoFunds, NoOre, Undiscovered, OutOfReach };

const ItemAmounts& beltBuildCost();

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
    ItemAmounts cost;                 // build cost of the new objects only
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
    PlacePlan planPlacement(const std::vector<PlaceRequest>& reqs, bool allowReplace,
                            const std::set<int>* protectedIds = nullptr, bool limitReach = false) const;
    // Does it (plan.status must be Ok). Returns the belt / machine id per request.
    std::vector<int> executePlacement(const std::vector<PlaceRequest>& reqs, const PlacePlan& plan);

    // ---- machines ----
    static Vec2i machineTopLeft(MachineType t, Vec2i mouseTile);
    PlaceStatus checkMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, ItemAmounts* cost = nullptr) const;
    // Returns the new machine id, or -1. `protectedIds` are never replaced.
    int placeMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, const std::set<int>* protectedIds,
                     ItemAmounts* delta = nullptr);
    bool canAffordMachineDeletion(int id) const;
    bool deleteMachineAt(Vec2i t, ItemAmounts* delta = nullptr);

    // Switching a machine's recipe hands its contents back to the player.
    // Refuses (returns false) if they don't fit.
    bool setRecipe(Machine& m, const Recipe* r);

    // ---- belts ----
    std::vector<Vec2i> dragTiles(Vec2i start, Vec2i end, Dir placementDir) const;
    std::vector<BeltPlan> planBelts(Vec2i start, Vec2i end, Dir placementDir) const;
    bool othersFreeOrMatching(const std::vector<BeltPlan>& plan, Vec2i start) const;
    bool isDragTileBlocked(const BeltPlan& p, Vec2i start, bool othersOk, bool allowReplace) const;
    bool anyDragTileBlocked(const std::vector<BeltPlan>& plan, Vec2i start, bool allowReplace) const;
    PlaceStatus beltAffordability(const std::vector<BeltPlan>& plan, ItemAmounts* cost = nullptr) const;
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
    };
    BlueprintCheck checkBlueprint(const Blueprint& bp, Vec2i topLeft, bool allowReplace = false) const;
    // All or nothing: needs free tiles (or identical objects, or Shift),
    // ore under new miners and the full cost.
    bool placeBlueprint(const Blueprint& bp, Vec2i topLeft, bool allowReplace = false, ItemAmounts* delta = nullptr);

    // ---- deleting everything in a rectangle (Ctrl+X) ----
    enum class AreaDelete { Ok, Nothing, NoSpace };
    // All or nothing: everything the selection takes (see gatherSelection,
    // max MAX_SELECTION x MAX_SELECTION) is removed and refunded - build cost plus contents - but
    // only if all of it fits into the inventory. `refunded` gets the items.
    AreaDelete deleteArea(Vec2i a, Vec2i b, ItemAmounts* refunded = nullptr, int* removed = nullptr);

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
