// sim/World.hpp - everything that exists in the world: terrain chunks, belts,
// machines, and the tile grid that maps each tile to whatever occupies it.
#pragma once
#include "../core/Types.hpp"
#include "../core/Items.hpp"
#include "../core/Inventory.hpp"
#include "../core/ItemFilter.hpp"
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Belts
// ---------------------------------------------------------------------------
struct Belt {
    bool alive = false;
    Vec2i pos;
    Dir dir = Dir::Right;

    // The single item on this tile (or NO_ITEM) and how far it has travelled
    // across the tile, in ticks: 0 = just entered, BELT_TICKS_PER_TILE = at
    // the tile centre, ready to leave.
    ItemId item = NO_ITEM;
    uint8_t progress = 0;
    Dir itemFrom = Dir::Right;   // travel direction it entered with (drawing)
    bool movedThisTick = false;  // for smooth drawing between ticks

    ItemFilter filter;           // what may ENTER this tile

    // Directions (as travel directions) from which something feeds this
    // tile. Bit i = Dir(i). Recomputed whenever the layout changes.
    uint8_t incomingMask = 0;
    uint8_t rr = 0;              // round-robin pointer for merges

    // Cached neighbour lookups (valid while the layout doesn't change):
    // what's in front of this tile, and what sits behind each incoming side.
    int32_t frontRef = 0;
    std::array<int32_t, 4> feedRef{};

    // Transport scratch, reset every tick: which pusher (machine/splitter)
    // this empty tile is holding its slot open for. -1 = none.
    int reservedFor = -1;
};

// ---------------------------------------------------------------------------
// Machines (smelter, assembler, miner, storage, splitter)
// ---------------------------------------------------------------------------
enum class MachineType : uint8_t { Smelter, Assembler, Splitter, Storage, Miner, Count };

// A miner digs one ore every 2 s; mining by hand is half that speed.
constexpr int MINER_TICKS = 2 * TICKS_PER_SECOND;
constexpr int HAND_MINE_TICKS = 2 * MINER_TICKS;

struct MachineSpec {
    const char* saveType;   // "smelter", ... (save file tag)
    const char* name;       // "Smelter", ... (UI)
    int w, h;               // footprint in tiles
    const char* sprite;
    ItemAmounts cost;       // build cost (refunded on delete)
};
const MachineSpec& machineSpec(MachineType t);

// Turning machines: a splitter has 4 directions; any other machine that is
// not square (the miner, 2 x 3) has 2 - standing (Right, as in the spec) or
// lying on its side (Down, w and h swapped); square machines have one.
// normalizeMachineDir maps any direction to one of those.
Dir normalizeMachineDir(MachineType t, Dir d);
bool machineTurnable(MachineType t);          // has more than one direction
Vec2i machineSize(MachineType t, Dir d);      // footprint in tiles for that direction
bool machineTypeFromSave(const std::string& s, MachineType& out);

// One input/output buffer of a producing machine: a 1x1 inventory that only
// ever holds `item`.
struct MachineBuffer {
    ItemId item = NO_ITEM;
    Inventory inv{1, 1};
};

// Purely visual: an item sliding from a belt into a machine.
struct InputAnim {
    ItemId item;
    float sx, sy, ex, ey;   // world pixels
    int ticks = 0;
    int duration = BELT_TICKS_PER_TILE;
};

struct Machine {
    bool alive = false;
    MachineType type = MachineType::Smelter;
    Vec2i pos;              // top-left tile
    int w = 1, h = 1;

    // --- producing machines (smelter, assembler, miner) ---
    const Recipe* recipe = nullptr;
    Recipe minerRecipe;      // a miner's own "dig ore" recipe
    ItemId oreItem = NO_ITEM;
    std::vector<MachineBuffer> inputs, outputs;
    bool processing = false;
    int timer = 0;           // ticks into the current batch

    // --- storage ---
    Inventory storage{8, 6};

    // --- splitter ---
    Dir dir = Dir::Right;
    ItemId item = NO_ITEM;
    uint8_t progress = 0;
    bool movedThisTick = false;
    uint8_t outputIndex = 0;              // round robin 0=left 1=fwd 2=right
    std::array<ItemFilter, 3> sideFilters;
    int reservedFor = -1;                 // transport scratch (like Belt)

    // Cached neighbour lookups, rebuilt with the layout:
    std::array<int32_t, 3> sideRef{};     // splitter: tile on each output side
    int32_t backRef = 0;                  // splitter: tile behind it
    std::vector<int> outBelts;            // producer: belts it can push onto

    std::vector<InputAnim> anims;

    bool isProducing() const {
        return type == MachineType::Smelter || type == MachineType::Assembler || type == MachineType::Miner;
    }
    bool isSplitter() const { return type == MachineType::Splitter; }
    bool isStorage() const { return type == MachineType::Storage; }
    RectF pixelRect() const {
        return {float(pos.x * CELL_SIZE), float(pos.y * CELL_SIZE), float(w * CELL_SIZE), float(h * CELL_SIZE)};
    }
    bool occupies(Vec2i t) const { return t.x >= pos.x && t.x < pos.x + w && t.y >= pos.y && t.y < pos.y + h; }

    // Splitter output side i (0 left, 1 forward, 2 right) as a direction.
    Dir sideDir(int i) const { return rotateCW(dir, i - 1); }

    MachineBuffer* inputBuffer(ItemId it);
    MachineBuffer* outputBuffer(ItemId it);
    void resetBuffers();                  // empty buffers matching `recipe`
    ItemAmounts contents() const;         // everything held inside
    ItemAmounts refundItems() const;      // build cost + contents
    bool canProcess() const;
    const std::vector<const Recipe*>* recipeChoices() const; // nullptr = none
};

std::unique_ptr<Machine> makeMachine(MachineType type, Vec2i topLeft, Dir dir = Dir::Right, ItemId ore = NO_ITEM);

// Every tile around a machine's footprint, with the outward direction.
// Same order as Python's _compute_output_tiles.
struct OutputTile { Vec2i tile; Dir dir; };
std::vector<OutputTile> machineOutputTiles(const Machine& m);

// ---------------------------------------------------------------------------
// Tile references and chunks
// ---------------------------------------------------------------------------
// A tile holds 0 (nothing), +(beltId+1) or -(machineId+1).
using TileRef = int32_t;

struct Chunk {
    std::array<TileRef, CHUNK_SIZE * CHUNK_SIZE> refs{};
    std::array<uint8_t, CHUNK_SIZE * CHUNK_SIZE> terrain{};
    bool terrainReady = false;
    int occupants = 0;
};

// ---------------------------------------------------------------------------
// Undo records: what one player action changed (see World::recordChanges and
// Builder::undo). `removed` keeps the full state of everything that was
// removed or changed (contents included); `added` only says what is there
// now (belt or machine type at a position).
// ---------------------------------------------------------------------------
struct ObjSnap {
    bool belt = false;
    Vec2i pos;
    MachineType type = MachineType::Smelter;   // machines
    Belt beltState;                            // belts (removed only)
    std::shared_ptr<const Machine> machine;    // machines (removed only)
    bool sameObject(const ObjSnap& o) const { return belt == o.belt && pos == o.pos && (belt || type == o.type); }
};
struct UndoRecord {
    std::vector<ObjSnap> removed, added;
    bool empty() const { return removed.empty() && added.empty(); }
};

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
class World {
public:
    int64_t seed = 0;

    // ---- undo recording ----
    // While on, every belt / machine that is added or removed (and every
    // one the builder changes in place, see noteChanged*) is written into
    // the pending record; takeRecord() hands it over and starts a new one.
    bool recordChanges = false;
    UndoRecord takeRecord() { UndoRecord r = std::move(record_); record_ = {}; return r; }
    void noteChangedBelt(int id);      // call BEFORE changing it in place
    void noteChangedMachine(int id);

    void clear();

    // ---- tile queries ----
    TileRef refAt(Vec2i t) const;
    Belt* beltAt(Vec2i t);
    Machine* machineAt(Vec2i t);
    int beltIdAt(Vec2i t) const { TileRef r = refAt(t); return r > 0 ? r - 1 : -1; }
    int machineIdAt(Vec2i t) const { TileRef r = refAt(t); return r < 0 ? -r - 1 : -1; }
    bool isCellBlocked(Vec2i t) const { return refAt(t) != 0; }

    // ---- terrain ----
    // Reading terrain generates the chunk if needed and marks it explored.
    // Only the normal (zoomed-in) view and building do this; the map view
    // only shows chunks that are already explored.
    uint8_t tileType(Vec2i t);
    ItemId oreAt(Vec2i t);
    bool isExplored(int cx, int cy) const { return explored_.count(Vec2i{cx, cy}) != 0; }
    void markExplored(int cx, int cy) { explored_.insert(Vec2i{cx, cy}); }
    const std::unordered_set<Vec2i, Vec2iHash>& explored() const { return explored_; }

    // ---- belts ----
    int addBelt(Vec2i pos, Dir dir);         // tile must be free
    void removeBelt(int id);
    Belt& belt(int id) { return belts_[size_t(id)]; }
    const Belt& belt(int id) const { return belts_[size_t(id)]; }
    std::vector<Belt>& belts() { return belts_; }
    const std::vector<Belt>& belts() const { return belts_; }

    // ---- machines ----
    int addMachine(std::unique_ptr<Machine> m); // footprint must be free
    void removeMachine(int id);
    Machine& machine(int id) { return *machines_[size_t(id)]; }
    const Machine& machine(int id) const { return *machines_[size_t(id)]; }
    size_t machineSlots() const { return machines_.size(); }
    bool machineAlive(int id) const { return id >= 0 && size_t(id) < machines_.size() && machines_[size_t(id)] && machines_[size_t(id)]->alive; }

    // ---- topology ----
    // Directions items can enter belt `b` from, given an optional "what if"
    // view (used by build previews). See computeIncoming in World.cpp.
    struct Overlay {
        // extra/override belts: tile -> direction (ghost belts)
        std::unordered_map<Vec2i, Dir, Vec2iHash> belts;
        // tiles to treat as empty (deletion previews)
        std::unordered_map<Vec2i, bool, Vec2iHash> removed;
        // pretend splitter (placement preview)
        bool hasSplitter = false; Vec2i splitterPos; Dir splitterDir = Dir::Right;
        // machine to ignore (deletion preview)
        int ignoreMachine = -1;
    };
    uint8_t computeIncoming(Vec2i pos, Dir dir, const Overlay* ov = nullptr) const;

    void markTopologyDirty() { topologyDirty_ = true; }
    void ensureTopology();      // recompute incoming masks + transport order

    // ---- simulation ----
    void tick();                // one fixed simulation step
    uint64_t tickCount() const { return tickCount_; }

    // Total items that exist in the world (belts, splitters, machines), for
    // conservation checks. Does not include build costs.
    ItemAmounts countItems() const;

    // Chunk access (for rendering / iteration).
    Chunk* chunk(int cx, int cy, bool create);
    const std::unordered_map<Vec2i, std::unique_ptr<Chunk>, Vec2iHash>& chunks() const { return chunks_; }

private:
    void setRef(Vec2i t, TileRef r);
    void ensureTerrain(Chunk& c, int cx, int cy);

    // transport (Transport.cpp)
    friend struct Transport;
    void rebuildTransportOrder();
    void transportTick();
    void machinesTick();

    std::unordered_map<Vec2i, std::unique_ptr<Chunk>, Vec2iHash> chunks_;
    std::unordered_set<Vec2i, Vec2iHash> explored_;
    std::vector<Belt> belts_;
    std::vector<int> freeBelts_;
    std::vector<std::unique_ptr<Machine>> machines_;
    std::vector<int> freeMachines_;

    bool topologyDirty_ = true;
    uint64_t tickCount_ = 0;
    UndoRecord record_;
    void recordRemoved(ObjSnap snap);
    void recordAdded(ObjSnap key);

    // Transport processing order, built by rebuildTransportOrder().
    // Nodes: belts are 0..B-1, splitters are B + machineId.
    struct Scc { int start; int len; uint8_t kind; }; // kind: 0 single, 1 simple belt cycle, 2 complex
    std::vector<int> order_;
    std::vector<Scc> sccs_;
    std::vector<int> cycleNext_;   // for simple cycles: next node in the cycle (by belt id)
    std::vector<int> producers_;   // machine ids that can push items out
    std::vector<int> producingIds_; // machine ids that run recipes
};
