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
// Bridges are belts one level up (`bridge`): they lie on their own layer
// above the ground, so they can cross belts (and water). They only ever
// connect straight: a bridge takes items from whatever points straight into
// its first tile (even if a belt lies under that tile) and hands them to the
// bridge in front of it or, at its end, to a belt / splitter pointing the
// same way or a machine. At most BRIDGE_MAX_LENGTH bridge tiles in a row.
constexpr int BRIDGE_MAX_LENGTH = 4;

struct Belt {
    bool alive = false;
    bool bridge = false;     // on the bridge layer (see above)
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
enum class MachineType : uint8_t { Smelter, Assembler, Splitter, Storage, Miner, WaterPump, Pipe, Count };

// ---------------------------------------------------------------------------
// Fluids. Pipes (1 x 1) connect to pipes and water pumps on all four sides;
// every connected group of pipes is one network = one tank of
// PIPE_CAPACITY per pipe holding one fluid (or nothing). Each pipe keeps its
// share (network amount / pipe count) so saving, splitting and deleting are
// simple: a deleted pipe takes its share with it. Water pumps hold up to
// PUMP_CAPACITY, make PUMP_RATE per second and put it straight into every
// connected network that is empty or holds water.
// ---------------------------------------------------------------------------
enum class Fluid : uint8_t { None, Water, Count };
const char* fluidName(Fluid f);        // "Water" ("" for None)
const char* fluidSaveId(Fluid f);      // "water"
Fluid fluidFromSaveId(const std::string& s);
constexpr double PIPE_CAPACITY = 100, PUMP_CAPACITY = 100, PUMP_RATE = 10;   // units, units, units per second
inline bool connectsPipes(MachineType t) { return t == MachineType::Pipe || t == MachineType::WaterPump; }

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

// Turning machines: splitters, smelters and assemblers have 4 directions;
// the miner (2 x 3) has 2 - standing (Right, as in the spec) or lying on its
// side (Down, w and h swapped); a storage chest has one.
// normalizeMachineDir maps any direction to one of those.
Dir normalizeMachineDir(MachineType t, Dir d);
bool machineTurnable(MachineType t);          // has more than one direction

// Output sides. Smelters and assemblers push only onto belts on their two
// output sides: `dir` and the side counter-clockwise of it (Right = right +
// top, the default; Down = bottom + right; Left = left + bottom; Up = top +
// left). Inputs only come in through the other two sides: a belt or splitter
// pointing into an output side can't deliver. Miners push onto every side;
// storage chests only onto belts pointing straight away from them.
inline bool machineHasOutputSides(MachineType t) { return t == MachineType::Smelter || t == MachineType::Assembler; }
inline bool isOutputSide(MachineType t, Dir machineDir, Dir side) {
    if (machineHasOutputSides(t)) return side == machineDir || side == rotateCW(machineDir, -1);
    return t == MachineType::Miner;   // (storage: see computeIncoming)
}
// Does a machine's direction matter for placing in place (an identical
// machine underneath) - splitters, smelters and assemblers.
inline bool machineDirMatters(MachineType t) { return t == MachineType::Splitter || machineHasOutputSides(t); }
// Mirroring left <-> right (blueprints): the output sides are mirrored too.
Dir mirroredMachineDir(MachineType t, Dir d);
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
    std::vector<Dir> outDirs;             // ... and the direction it pushes in

    std::vector<InputAnim> anims;

    // --- fluids (pipes: their share of the network; water pumps: their buffer) ---
    Fluid fluid = Fluid::None;
    double fluidAmount = 0;
    int fluidNet = -1;                    // pipes: network index (rebuilt with the layout)

    bool isProducing() const {
        return type == MachineType::Smelter || type == MachineType::Assembler || type == MachineType::Miner;
    }
    bool isSplitter() const { return type == MachineType::Splitter; }
    bool isPipe() const { return type == MachineType::Pipe; }
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
// The same tiles without building a vector: f(OutputTile).
template <class F> void forEachOutputTile(const Machine& m, F f) {
    for (int dy = 0; dy < m.h; ++dy) {
        f(OutputTile{{m.pos.x + m.w, m.pos.y + dy}, Dir::Right});
        f(OutputTile{{m.pos.x - 1, m.pos.y + dy}, Dir::Left});
    }
    for (int dx = 0; dx < m.w; ++dx) {
        f(OutputTile{{m.pos.x + dx, m.pos.y - 1}, Dir::Up});
        f(OutputTile{{m.pos.x + dx, m.pos.y + m.h}, Dir::Down});
    }
}

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
    uint32_t version = 0;   // +1 whenever a tile ref changes (map view caches)
};

// Tile -> chunk coordinates / index inside the chunk (floor division, so
// negative tiles work: -1 -> chunk -1, index 31).
inline Vec2i chunkOf(Vec2i t) { return {t.x >> CHUNK_SHIFT, t.y >> CHUNK_SHIFT}; }
inline size_t tileIndex(Vec2i t) { return size_t((t.y & (CHUNK_SIZE - 1)) * CHUNK_SIZE + (t.x & (CHUNK_SIZE - 1))); }

// ---------------------------------------------------------------------------
// Undo records: what one player action changed (see World::recordChanges and
// Builder::undo). `removed` keeps the full state of everything that was
// removed or changed (contents included); `added` only says what is there
// now (belt or machine type at a position).
// ---------------------------------------------------------------------------
struct ObjSnap {
    bool belt = false;
    bool bridge = false;                       // a belt on the bridge layer
    Vec2i pos;
    MachineType type = MachineType::Smelter;   // machines
    Belt beltState;                            // belts (removed only)
    std::shared_ptr<const Machine> machine;    // machines (removed only)
    bool sameObject(const ObjSnap& o) const { return belt == o.belt && pos == o.pos && (belt ? bridge == o.bridge : type == o.type); }
};
struct UndoRecord {
    std::vector<ObjSnap> removed, added;
    std::vector<Vec2i> landfillAdded, landfillRemoved;   // water tiles filled / turned back into water
    bool empty() const { return removed.empty() && added.empty() && landfillAdded.empty() && landfillRemoved.empty(); }
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
    UndoRecord takeRecord() { UndoRecord r = std::move(record_); record_ = {}; addedKeys_.clear(); return r; }
    void noteChangedBelt(int id);      // call BEFORE changing it in place
    void noteChangedMachine(int id);

    void clear();

    // ---- tile queries ----
    TileRef refAt(Vec2i t) const;
    Belt* beltAt(Vec2i t);
    Machine* machineAt(Vec2i t);
    int beltIdAt(Vec2i t) const { TileRef r = refAt(t); return r > 0 ? r - 1 : -1; }
    int machineIdAt(Vec2i t) const { TileRef r = refAt(t); return r < 0 ? -r - 1 : -1; }
    bool isCellBlocked(Vec2i t) const { return refAt(t) != 0; }   // (ground layer only)

    // ---- bridge layer (belts with `bridge` set; see Belt) ----
    int bridgeIdAt(Vec2i t) const { auto it = bridges_.find(t); return it == bridges_.end() ? -1 : it->second; }
    Belt* bridgeAt(Vec2i t) { int id = bridgeIdAt(t); return id >= 0 ? &belts_[size_t(id)] : nullptr; }
    const std::unordered_map<Vec2i, int, Vec2iHash>& bridges() const { return bridges_; }
    // The bridge at t pointing `d` if it is the first tile of its bridge
    // (no bridge pointing `d` right behind it), as a TileRef; else 0.
    TileRef bridgeStartRef(Vec2i t, Dir d) const;
    // How many bridge tiles pointing `d` lie in a row through t (0 if none at t pointing d).
    int bridgeRunLength(Vec2i t, Dir d) const;
    // Bridges can't float: a bridge tile is supported if the first tile of
    // its bridge has a belt, splitter or machine (ground) pointing straight
    // into it. groundPushes: does the ground at `src` push items `travel`
    // onto the next tile?
    bool groundPushes(Vec2i src, Dir travel) const;
    bool bridgeSupported(int id) const;
    std::vector<int> floatingBridges() const;   // every bridge that isn't supported
    // ---- terrain ----
    // Reading terrain generates the chunk if needed and marks it explored.
    // Only the normal (zoomed-in) view and building do this; the map view
    // only shows chunks that are already explored.
    uint8_t tileType(Vec2i t);
    ItemId oreAt(Vec2i t);
    bool isWater(Vec2i t);   // lakes / seas (not landfilled): nothing can be built there

    // ---- landfill: water tiles turned into land (saved; recorded for undo) ----
    bool isLandfill(Vec2i t) const { return landfill_.count(t) != 0; }
    const std::unordered_set<Vec2i, Vec2iHash>& landfill() const { return landfill_; }
    void addLandfill(Vec2i t);
    void removeLandfill(Vec2i t);
    bool isExplored(int cx, int cy) const { return explored_.count(Vec2i{cx, cy}) != 0; }
    void markExplored(int cx, int cy) { explored_.insert(Vec2i{cx, cy}); }
    const std::unordered_set<Vec2i, Vec2iHash>& explored() const { return explored_; }

    // ---- belts ----
    int addBelt(Vec2i pos, Dir dir);         // tile must be free
    int addBridge(Vec2i pos, Dir dir);       // bridge layer at pos must be free
    void removeBelt(int id);                 // (bridges too)
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
    // Which sides feed belt `id` (ground or bridge) and what feeds them.
    uint8_t computeFeeds(const Belt& b, std::array<int32_t, 4>* refs) const;


    void markTopologyDirty() { topologyDirty_ = true; }
    void ensureTopology();      // recompute incoming masks + transport order

    // ---- fluids (see Fluid) ----
    struct FluidNet {
        std::vector<int> pipes;    // machine ids
        std::vector<int> pumps;    // water pumps touching it
        Fluid fluid = Fluid::None;
        double amount = 0;
        double capacity() const { return PIPE_CAPACITY * double(pipes.size()); }
    };
    const std::vector<FluidNet>& fluidNets() { ensureTopology(); return fluidNets_; }
    const FluidNet* fluidNetOf(int pipeId);          // nullptr if not a pipe
    // Which sides of tile t have a pipe / water pump (for a pipe at t): bit i = Dir(i).
    uint8_t pipeMask(Vec2i t) const;

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
    // Chunk lookup with a one-entry cache: neighbouring tiles are almost
    // always in the same chunk, so most lookups skip the hash map. (Chunks
    // are only ever removed by clear(), which resets the cache.)
    Chunk* findChunk(Vec2i key) const {
        if (lastChunk_ && key == lastChunkKey_) return lastChunk_;
        auto it = chunks_.find(key);
        if (it == chunks_.end()) return nullptr;
        lastChunkKey_ = key;
        lastChunk_ = it->second.get();
        return lastChunk_;
    }
    mutable Vec2i lastChunkKey_;
    mutable Chunk* lastChunk_ = nullptr;

    // transport (Transport.cpp)
    friend struct Transport;
    void rebuildTransportOrder();
    void transportTick();
    void machinesTick();
    void fluidTick();
    void rebuildFluidNets();
    void shareFluid(FluidNet& n);   // every pipe of n gets its share
    std::vector<FluidNet> fluidNets_;

    std::unordered_map<Vec2i, std::unique_ptr<Chunk>, Vec2iHash> chunks_;
    std::unordered_set<Vec2i, Vec2iHash> explored_;
    std::unordered_set<Vec2i, Vec2iHash> landfill_;
    std::unordered_map<Vec2i, int, Vec2iHash> bridges_;   // tile -> belt id (bridge layer)
    std::vector<Belt> belts_;
    std::vector<int> freeBelts_;
    std::vector<std::unique_ptr<Machine>> machines_;
    std::vector<int> freeMachines_;

    bool topologyDirty_ = true;

    uint64_t tickCount_ = 0;
    UndoRecord record_;
    // What record_.added holds, for quick "added in this record?" checks
    // (ObjSnap::sameObject: belt or machine type, at a position).
    struct SnapKey {
        Vec2i pos; uint8_t kind;   // MachineType, or 255 for a belt
        bool operator==(const SnapKey& o) const { return pos == o.pos && kind == o.kind; }
    };
    struct SnapKeyHash { size_t operator()(const SnapKey& k) const { return Vec2iHash()(k.pos) ^ (size_t(k.kind) * 0x9e3779b97f4a7c15ULL); } };
    static SnapKey snapKey(const ObjSnap& s) { return {s.pos, uint8_t(s.belt ? (s.bridge ? 254 : 255) : int(s.type))}; }
    std::unordered_map<SnapKey, int, SnapKeyHash> addedKeys_;   // -> how many entries
    bool inAdded(const ObjSnap& s) const { return addedKeys_.count(snapKey(s)) != 0; }
    void recordRemoved(ObjSnap snap);
    void recordAdded(ObjSnap key);

    // Transport processing order, built by rebuildTransportOrder().
    // Nodes: belts are 0..B-1, splitters are B + machineId.
    struct Scc { int start; int len; uint8_t kind; }; // kind: 0 single, 1 simple belt cycle, 2 complex
    std::vector<int> order_;
    std::vector<Scc> sccs_;
    std::vector<int> cycleNext_;   // for simple cycles: next node in the cycle (by belt id)
    std::vector<int> producers_;   // machine ids that can push items out
    std::vector<int> splitterIds_; // machine ids of splitters
    std::vector<int> producingIds_; // machine ids that run recipes
};
