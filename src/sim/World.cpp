// sim/World.cpp - world storage, machines, and belt topology.
#include "World.hpp"
#include "Terrain.hpp"
#include <algorithm>

// ---------------------------------------------------------------------------
// Machine specs
// ---------------------------------------------------------------------------
const MachineSpec& machineSpec(MachineType t) {
    static const ItemId iron = itemByStringId("iron_ingot");
    static const ItemId copper = itemByStringId("copper_ingot");
    static const MachineSpec specs[] = {
        {"smelter", "Smelter", 3, 3, "assets/sprites/machines/smelter.png", {{iron, 2}, {copper, 1}}},
        {"assembler", "Assembler", 3, 3, "assets/sprites/machines/assembler.png", {{iron, 3}}},
        {"splitter", "Splitter", 1, 1, "assets/sprites/machines/splitter.png", {{iron, 4}}},
        {"storage", "Storage", 2, 2, "assets/sprites/machines/storage.png", {{iron, 10}}},
        {"miner", "Miner", 2, 3, "assets/sprites/machines/miner.png", {{iron, 5}}},
        // must stand right next to water; does nothing yet (water will go into pipes)
        {"water_pump", "Water Pump", 1, 2, "assets/sprites/machines/water_pump.png", {{iron, 5}, {copper, 3}}},
        {"pipe", "Pipe", 1, 1, "assets/sprites/pipes/pipe_single.png", {{iron, 1}}},
    };
    return specs[int(t)];
}

bool machineTypeFromSave(const std::string& s, MachineType& out) {
    for (int i = 0; i < int(MachineType::Count); ++i) {
        if (s == machineSpec(MachineType(i)).saveType) { out = MachineType(i); return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------
MachineBuffer* Machine::inputBuffer(ItemId it) {
    for (auto& b : inputs) if (b.item == it) return &b;
    return nullptr;
}
MachineBuffer* Machine::outputBuffer(ItemId it) {
    for (auto& b : outputs) if (b.item == it) return &b;
    return nullptr;
}

void Machine::resetBuffers() {
    inputs.clear();
    outputs.clear();
    if (!recipe) return;
    for (auto& p : recipe->inputs) inputs.push_back({p.first, Inventory(1, 1)});
    for (auto& p : recipe->outputs) outputs.push_back({p.first, Inventory(1, 1)});
}

ItemAmounts Machine::contents() const {
    ItemAmounts out;
    for (auto& b : inputs) for (auto& p : b.inv.contents()) addAmount(out, p.first, p.second);
    for (auto& b : outputs) for (auto& p : b.inv.contents()) addAmount(out, p.first, p.second);
    if (isStorage()) for (auto& p : storage.contents()) addAmount(out, p.first, p.second);
    if (isSplitter() && item != NO_ITEM) addAmount(out, item, 1);
    return out;
}

ItemAmounts Machine::refundItems() const {
    ItemAmounts out = machineSpec(type).cost;
    for (auto& p : contents()) addAmount(out, p.first, p.second);
    return out;
}

bool Machine::canProcess() const {
    if (!recipe) return false;
    for (auto& p : recipe->inputs) {
        bool found = false;
        for (auto& b : inputs)
            if (b.item == p.first) { found = true; if (b.inv.amount(p.first) < p.second) return false; }
        if (!found) return false;
    }
    for (auto& p : recipe->outputs) {
        bool found = false;
        for (auto& b : outputs)
            if (b.item == p.first) { found = true; if (!b.inv.canAdd(p.first, p.second)) return false; }
        if (!found) return false;
    }
    return true;
}

const std::vector<const Recipe*>* Machine::recipeChoices() const {
    if (type == MachineType::Smelter) return &smelterRecipes();
    if (type == MachineType::Assembler) return &assemblerRecipes();
    return nullptr;
}

Dir normalizeMachineDir(MachineType t, Dir d) {
    if (t == MachineType::Splitter || machineHasOutputSides(t)) return d;
    const MachineSpec& s = machineSpec(t);
    if (s.w == s.h) return Dir::Right;
    return (d == Dir::Down || d == Dir::Up) ? Dir::Down : Dir::Right;
}

bool machineTurnable(MachineType t) { return machineDirMatters(t) || machineSpec(t).w != machineSpec(t).h; }

Dir mirroredMachineDir(MachineType t, Dir d) {
    auto flip = [](Dir x) { return x == Dir::Left ? Dir::Right : x == Dir::Right ? Dir::Left : x; };
    // output sides {d, ccw(d)} mirrored are {flip(ccw(d)), flip(d)} = {e, ccw(e)} with e = flip(ccw(d))
    if (machineHasOutputSides(t)) return flip(rotateCW(d, -1));
    return normalizeMachineDir(t, flip(d));
}

Vec2i machineSize(MachineType t, Dir d) {
    const MachineSpec& s = machineSpec(t);
    return normalizeMachineDir(t, d) == Dir::Down && t != MachineType::Splitter ? Vec2i{s.h, s.w} : Vec2i{s.w, s.h};
}

std::unique_ptr<Machine> makeMachine(MachineType type, Vec2i topLeft, Dir dir, ItemId ore) {
    auto m = std::make_unique<Machine>();
    dir = normalizeMachineDir(type, dir);
    Vec2i size = machineSize(type, dir);
    m->type = type;
    m->pos = topLeft;
    m->w = size.x;
    m->h = size.y;
    m->dir = dir;
    if (type == MachineType::Smelter) m->recipe = smelterRecipes().front();
    if (type == MachineType::Assembler) m->recipe = assemblerRecipes().front();
    if (type == MachineType::Miner) {
        m->oreItem = ore;
        Recipe& r = m->minerRecipe;
        r.id = "mine_" + itemDef(ore).id;
        r.name = "Mine " + itemDef(ore).id;
        r.outputs = {{ore, 1}};
        r.ticks = MINER_TICKS;
        m->recipe = &m->minerRecipe;
    }
    m->resetBuffers();
    return m;
}

std::vector<OutputTile> machineOutputTiles(const Machine& m) {
    std::vector<OutputTile> tiles;
    tiles.reserve(size_t(2 * (m.w + m.h)));
    forEachOutputTile(m, [&](const OutputTile& ot) { tiles.push_back(ot); });
    return tiles;
}

// ---------------------------------------------------------------------------
// World: chunks & tiles
// ---------------------------------------------------------------------------
void World::clear() {
    chunks_.clear();
    explored_.clear();
    landfill_.clear();
    bridges_.clear();
    belts_.clear();
    freeBelts_.clear();
    machines_.clear();
    freeMachines_.clear();
    topologyDirty_ = true;
    lastChunk_ = nullptr;
    tickCount_ = 0;
    record_ = {};
    addedKeys_.clear();
    terrain::clearCache();
}

Chunk* World::chunk(int cx, int cy, bool create) {
    Vec2i key{cx, cy};
    if (Chunk* c = findChunk(key)) return c;
    if (!create) return nullptr;
    auto c = std::make_unique<Chunk>();
    Chunk* raw = c.get();
    chunks_.emplace(key, std::move(c));
    return raw;
}

TileRef World::refAt(Vec2i t) const {
    const Chunk* c = findChunk(chunkOf(t));
    return c ? c->refs[tileIndex(t)] : 0;
}

void World::setRef(Vec2i t, TileRef r) {
    Vec2i k = chunkOf(t);
    Chunk* c = chunk(k.x, k.y, true);
    TileRef& slot = c->refs[tileIndex(t)];
    if (slot == 0 && r != 0) c->occupants++;
    if (slot != 0 && r == 0) c->occupants--;
    slot = r;
    ++c->version;
}

Belt* World::beltAt(Vec2i t) {
    TileRef r = refAt(t);
    return r > 0 ? &belts_[size_t(r - 1)] : nullptr;
}

Machine* World::machineAt(Vec2i t) {
    TileRef r = refAt(t);
    return r < 0 ? machines_[size_t(-r - 1)].get() : nullptr;
}

void World::ensureTerrain(Chunk& c, int cx, int cy) {
    if (c.terrainReady) return;
    terrain::generateChunk(seed, cx, cy, c.terrain.data());
    c.terrainReady = true;
    explored_.insert(Vec2i{cx, cy});
}

uint8_t World::tileType(Vec2i t) {
    Vec2i k = chunkOf(t);
    Chunk* c = chunk(k.x, k.y, true);
    ensureTerrain(*c, k.x, k.y);
    return c->terrain[tileIndex(t)];
}

ItemId World::oreAt(Vec2i t) { return terrain::oreForTileType(tileType(t)); }
bool World::isWater(Vec2i t) { return terrain::isWaterTile(tileType(t)) && !landfill_.count(t); }

void World::addLandfill(Vec2i t) {
    if (!landfill_.insert(t).second) return;
    Vec2i k = chunkOf(t);
    ++chunk(k.x, k.y, true)->version;   // the map view redraws that chunk
    if (!recordChanges) return;
    auto& rm = record_.landfillRemoved;   // removed earlier in the same action: cancels out
    auto it = std::find(rm.begin(), rm.end(), t);
    if (it != rm.end()) rm.erase(it);
    else record_.landfillAdded.push_back(t);
}

void World::removeLandfill(Vec2i t) {
    if (!landfill_.erase(t)) return;
    Vec2i k = chunkOf(t);
    ++chunk(k.x, k.y, true)->version;
    if (!recordChanges) return;
    auto& ad = record_.landfillAdded;
    auto it = std::find(ad.begin(), ad.end(), t);
    if (it != ad.end()) ad.erase(it);
    else record_.landfillRemoved.push_back(t);
}

// ---------------------------------------------------------------------------
// Undo recording
// ---------------------------------------------------------------------------
static ObjSnap beltSnap(const Belt& b) {
    ObjSnap s;
    s.belt = true;
    s.bridge = b.bridge;
    s.pos = b.pos;
    s.beltState = b;
    return s;
}
static ObjSnap machineSnap(const Machine& m) {
    ObjSnap s;
    s.pos = m.pos;
    s.type = m.type;
    s.machine = std::make_shared<Machine>(m);
    return s;
}

// Something that was added earlier in the same record and is removed again
// cancels out; otherwise its old state is kept.
void World::recordRemoved(ObjSnap snap) {
    if (inAdded(snap))
        for (size_t i = 0; i < record_.added.size(); ++i)
            if (record_.added[i].sameObject(snap)) {
                auto k = addedKeys_.find(snapKey(snap));
                if (--k->second == 0) addedKeys_.erase(k);
                record_.added.erase(record_.added.begin() + long(i));
                return;
            }
    record_.removed.push_back(std::move(snap));
}

void World::recordAdded(ObjSnap key) {
    key.machine.reset();
    key.beltState = Belt{};
    ++addedKeys_[snapKey(key)];
    record_.added.push_back(std::move(key));
}

void World::noteChangedBelt(int id) {
    if (!recordChanges) return;
    ObjSnap s = beltSnap(belts_[size_t(id)]);
    if (inAdded(s)) return;   // new in this record anyway
    record_.removed.push_back(s);
    recordAdded(s);
}

void World::noteChangedMachine(int id) {
    if (!recordChanges || !machineAlive(id)) return;
    ObjSnap s = machineSnap(*machines_[size_t(id)]);
    if (inAdded(s)) return;
    record_.removed.push_back(s);
    recordAdded(s);
}

// ---------------------------------------------------------------------------
// Belts & machines
// ---------------------------------------------------------------------------
int World::addBelt(Vec2i pos, Dir dir) {
    int id;
    if (!freeBelts_.empty()) { id = freeBelts_.back(); freeBelts_.pop_back(); }
    else { id = int(belts_.size()); belts_.emplace_back(); }
    Belt& b = belts_[size_t(id)];
    b = Belt{};
    b.alive = true;
    b.pos = pos;
    b.dir = dir;
    b.itemFrom = dir;
    setRef(pos, TileRef(id + 1));
    topologyDirty_ = true;
    if (recordChanges) recordAdded(beltSnap(b));
    return id;
}

int World::addBridge(Vec2i pos, Dir dir) {
    int id;
    if (!freeBelts_.empty()) { id = freeBelts_.back(); freeBelts_.pop_back(); }
    else { id = int(belts_.size()); belts_.emplace_back(); }
    Belt& b = belts_[size_t(id)];
    b = Belt{};
    b.alive = true;
    b.bridge = true;
    b.pos = pos;
    b.dir = dir;
    b.itemFrom = dir;
    bridges_[pos] = id;
    Vec2i k = chunkOf(pos);
    ++chunk(k.x, k.y, true)->version;   // the map view redraws that chunk
    topologyDirty_ = true;
    if (recordChanges) recordAdded(beltSnap(b));
    return id;
}

TileRef World::bridgeStartRef(Vec2i t, Dir d) const {
    int id = bridgeIdAt(t);
    if (id < 0 || belts_[size_t(id)].dir != d) return 0;
    int prev = bridgeIdAt(t - dirVec(d));
    if (prev >= 0 && belts_[size_t(prev)].dir == d) return 0;   // the middle of a bridge
    return TileRef(id + 1);
}

int World::bridgeRunLength(Vec2i t, Dir d) const {
    auto pointing = [&](Vec2i p) { int id = bridgeIdAt(p); return id >= 0 && belts_[size_t(id)].dir == d; };
    if (!pointing(t)) return 0;
    int n = 1;
    for (Vec2i p = t - dirVec(d); pointing(p) && n <= 64; p = p - dirVec(d)) ++n;
    for (Vec2i p = t + dirVec(d); pointing(p) && n <= 64; p = p + dirVec(d)) ++n;
    return n;
}

bool World::groundPushes(Vec2i src, Dir travel) const {
    TileRef r = refAt(src);
    if (r > 0) return belts_[size_t(r - 1)].dir == travel;
    if (r < 0) {
        const Machine& m = *machines_[size_t(-r - 1)];
        if (m.isSplitter()) return travel != opposite(m.dir);
        if (m.isStorage()) return true;   // (straight away - the next tile is straight away)
        return isOutputSide(m.type, m.dir, travel);
    }
    return false;
}

bool World::bridgeSupported(int id) const {
    const Belt& b = belts_[size_t(id)];
    Vec2i p = b.pos;
    for (int n = 0; n < 64; ++n) {   // back to the first tile of the bridge
        int prev = bridgeIdAt(p - dirVec(b.dir));
        if (prev < 0 || belts_[size_t(prev)].dir != b.dir) break;
        p = p - dirVec(b.dir);
    }
    return groundPushes(p - dirVec(b.dir), b.dir);
}

std::vector<int> World::floatingBridges() const {
    std::vector<int> out;
    for (auto& kv : bridges_)
        if (!bridgeSupported(kv.second)) out.push_back(kv.second);
    std::sort(out.begin(), out.end());
    return out;
}

void World::removeBelt(int id) {
    Belt& b = belts_[size_t(id)];
    if (!b.alive) return;
    if (recordChanges) recordRemoved(beltSnap(b));
    if (b.bridge) {
        bridges_.erase(b.pos);
        Vec2i k = chunkOf(b.pos);
        ++chunk(k.x, k.y, true)->version;
    } else {
        setRef(b.pos, 0);
    }
    b = Belt{};
    freeBelts_.push_back(id);
    topologyDirty_ = true;
}

int World::addMachine(std::unique_ptr<Machine> m) {
    int id;
    if (!freeMachines_.empty()) { id = freeMachines_.back(); freeMachines_.pop_back(); }
    else { id = int(machines_.size()); machines_.emplace_back(); }
    m->alive = true;
    for (int dy = 0; dy < m->h; ++dy)
        for (int dx = 0; dx < m->w; ++dx)
            setRef({m->pos.x + dx, m->pos.y + dy}, TileRef(-(id + 1)));
    // A miner's recipe lives inside the machine; re-point after the move.
    if (m->type == MachineType::Miner) m->recipe = &m->minerRecipe;
    machines_[size_t(id)] = std::move(m);
    topologyDirty_ = true;
    if (recordChanges) {
        ObjSnap key;
        key.pos = machines_[size_t(id)]->pos;
        key.type = machines_[size_t(id)]->type;
        recordAdded(key);
    }
    return id;
}

void World::removeMachine(int id) {
    if (!machineAlive(id)) return;
    Machine& m = *machines_[size_t(id)];
    if (recordChanges) recordRemoved(machineSnap(m));
    for (int dy = 0; dy < m.h; ++dy)
        for (int dx = 0; dx < m.w; ++dx)
            setRef({m.pos.x + dx, m.pos.y + dy}, 0);
    machines_[size_t(id)].reset();
    freeMachines_.push_back(id);
    topologyDirty_ = true;
}

// ---------------------------------------------------------------------------
// Incoming directions (which sides feed a belt tile)
// ---------------------------------------------------------------------------
// What can feed a belt tile from side `travel` (items moving `travel`).
// Ground belts: a belt / splitter pointing at it, a machine output side, a
// bridge ending straight into it - but whatever points straight into the
// first tile of a bridge lying on top of this tile feeds the bridge instead.
// Bridges: only straight from behind - the bridge behind them, or (first
// tile) a belt / splitter / machine pushing straight in.
static uint8_t feedsCore(const World& w, const std::vector<Belt>& belts, const std::vector<std::unique_ptr<Machine>>& machines,
                         Vec2i pos, Dir dir, bool bridge, const World::Overlay* ov, std::array<int32_t, 4>* refs) {
    uint8_t mask = 0;
    if (refs) refs->fill(0);
    auto feed = [&](Dir travel, TileRef r) {
        mask |= dirBit(travel);
        if (refs) (*refs)[size_t(travel)] = r;
    };
    // a ground thing at `src` pushing `travel` (belt, splitter, machine)
    auto groundFeeds = [&](Vec2i src, Dir travel, bool toBridge, bool straightOnly) -> TileRef {
        TileRef r = w.refAt(src);
        if (r > 0) return (belts[size_t(r - 1)].dir == travel && !toBridge) ? r : 0;
        if (r < 0) {
            const Machine& m = *machines[size_t(-r - 1)];
            if (ov && ov->ignoreMachine == -r - 1) return 0;
            if (m.isSplitter()) return (travel != opposite(m.dir) && !toBridge) ? r : 0;
            if (m.isStorage()) return travel == dir ? r : 0;   // a chest only pushes straight away
            if (isOutputSide(m.type, m.dir, travel) && (!straightOnly || travel == dir)) return r;
        }
        return 0;
    };
    if (bridge) {
        Dir travel = dir;
        Vec2i src = pos - dirVec(travel);
        int bid = w.bridgeIdAt(src);
        if (bid >= 0 && belts[size_t(bid)].dir == travel) { feed(travel, TileRef(bid + 1)); return mask; }
        if (TileRef r = groundFeeds(src, travel, false, true)) feed(travel, r);
        return mask;
    }
    for (int i = 0; i < 4; ++i) {
        Dir travel = Dir(i);                  // direction items would move
        if (travel == opposite(dir)) continue; // never from straight ahead
        Vec2i src = pos - dirVec(travel);     // tile they would come from

        bool removed = false;
        if (ov) {
            auto it = ov->belts.find(src);    // ghost belt
            if (it != ov->belts.end()) {
                if (it->second == travel) mask |= dirBit(travel);
                continue;
            }
            removed = ov->removed.count(src) != 0;
        }
        if (travel == dir) {                  // a bridge ending straight into this belt
            int bid = w.bridgeIdAt(src);
            if (bid >= 0 && belts[size_t(bid)].dir == travel) { feed(travel, TileRef(bid + 1)); continue; }
        }
        bool toBridge = w.bridgeStartRef(pos, travel) != 0;   // a bridge starts on this tile: it gets them
        if (ov && ov->hasSplitter && ov->splitterPos == src) {
            if (travel != opposite(ov->splitterDir) && !toBridge) mask |= dirBit(travel);
            continue;
        }
        if (removed) continue;
        if (TileRef r = groundFeeds(src, travel, toBridge, false)) feed(travel, r);
    }
    return mask;
}

uint8_t World::computeIncoming(Vec2i pos, Dir dir, const Overlay* ov) const {
    return feedsCore(*this, belts_, machines_, pos, dir, false, ov, nullptr);
}

uint8_t World::computeFeeds(const Belt& b, std::array<int32_t, 4>* refs) const {
    return feedsCore(*this, belts_, machines_, b.pos, b.dir, b.bridge, nullptr, refs);
}

void World::ensureTopology() {
    if (!topologyDirty_) return;
    // 1) neighbours that don't depend on the incoming masks
    splitterIds_.clear();
    for (auto& b : belts_) {
        if (!b.alive) continue;
        Vec2i q = b.pos + dirVec(b.dir);
        if (!b.bridge) {   // a bridge starting in front (pointing the same way) takes the items
            TileRef s0 = bridgeStartRef(q, b.dir);
            b.frontRef = s0 ? s0 : refAt(q);
            continue;
        }
        // a bridge: only straight on - the next bridge tile, or at its end a
        // belt / splitter pointing the same way, or a machine
        b.frontRef = 0;
        int nb = bridgeIdAt(q);
        if (nb >= 0) {
            if (belts_[size_t(nb)].dir == b.dir) b.frontRef = TileRef(nb + 1);
            continue;
        }
        TileRef r = refAt(q);
        if (r > 0 && belts_[size_t(r - 1)].dir == b.dir) b.frontRef = r;
        else if (r < 0 && (!machines_[size_t(-r - 1)]->isSplitter() || machines_[size_t(-r - 1)]->dir == b.dir)) b.frontRef = r;
    }
    for (int mid = 0; mid < int(machines_.size()); ++mid) {
        if (!machineAlive(mid) || !machines_[size_t(mid)]->isSplitter()) continue;
        Machine& m = *machines_[size_t(mid)];
        splitterIds_.push_back(mid);
        for (int i = 0; i < 3; ++i) {
            Vec2i t = m.pos + dirVec(m.sideDir(i));
            TileRef s0 = bridgeStartRef(t, m.sideDir(i));   // a bridge starting there gets it
            m.sideRef[size_t(i)] = s0 ? s0 : refAt(t);
        }
        Vec2i back = m.pos - dirVec(m.dir);
        int bb = bridgeIdAt(back);   // a bridge ending straight into the splitter
        m.backRef = (bb >= 0 && belts_[size_t(bb)].dir == m.dir) ? TileRef(bb + 1) : refAt(back);
    }
    // 2) incoming masks
    for (auto& b : belts_) {
        if (!b.alive) continue;
        uint8_t old = b.incomingMask;
        b.incomingMask = computeFeeds(b, &b.feedRef);
        if (old != b.incomingMask && !(b.incomingMask & dirBit(Dir(b.rr % 4)))) {
            // keep favouring the same side if it still exists, else restart
            b.rr = 0;
        }
    }
    // 3) what each machine pushes onto (computeIncoming decided that)
    for (auto& mp : machines_) {
        if (!mp || !mp->alive || mp->isSplitter()) continue;
        Machine& m = *mp;
        m.outBelts.clear();
        m.outDirs.clear();
        forEachOutputTile(m, [&](const OutputTile& ot) {
            TileRef r = refAt(ot.tile);
            if (r > 0 && (belts_[size_t(r - 1)].incomingMask & dirBit(ot.dir))) {
                m.outBelts.push_back(r - 1);
                m.outDirs.push_back(ot.dir);
            }
            int bid = bridgeIdAt(ot.tile);   // a bridge starting there, pointing straight away
            if (bid >= 0 && (belts_[size_t(bid)].incomingMask & dirBit(ot.dir)) && belts_[size_t(bid)].feedRef[size_t(ot.dir)] < 0) {
                m.outBelts.push_back(bid);
                m.outDirs.push_back(ot.dir);
            }
        });
    }
    rebuildTransportOrder();
    rebuildFluidNets();
    topologyDirty_ = false;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void World::tick() {
    ensureTopology();
    transportTick();
    machinesTick();
    fluidTick();
    ++tickCount_;
}

ItemAmounts World::countItems() const {
    ItemAmounts out;
    for (auto& b : belts_)
        if (b.alive && b.item != NO_ITEM) addAmount(out, b.item, 1);
    for (auto& m : machines_)
        if (m && m->alive)
            for (auto& p : m->contents()) addAmount(out, p.first, p.second);
    return out;
}

// ---------------------------------------------------------------------------
// Fluids
// ---------------------------------------------------------------------------
const char* fluidName(Fluid f) { return f == Fluid::Water ? "Water" : ""; }
const char* fluidSaveId(Fluid f) { return f == Fluid::Water ? "water" : ""; }
Fluid fluidFromSaveId(const std::string& s) { return s == "water" ? Fluid::Water : Fluid::None; }

uint8_t World::pipeMask(Vec2i t) const {
    uint8_t mask = 0;
    for (int i = 0; i < 4; ++i) {
        TileRef r = refAt(t + dirVec(Dir(i)));
        if (r < 0 && connectsPipes(machines_[size_t(-r - 1)]->type)) mask |= dirBit(Dir(i));
    }
    return mask;
}

const World::FluidNet* World::fluidNetOf(int id) {
    ensureTopology();
    if (!machineAlive(id) || !machines_[size_t(id)]->isPipe()) return nullptr;
    int n = machines_[size_t(id)]->fluidNet;
    return n >= 0 && size_t(n) < fluidNets_.size() ? &fluidNets_[size_t(n)] : nullptr;
}

void World::shareFluid(FluidNet& n) {
    if (n.amount <= 1e-9) { n.amount = 0; n.fluid = Fluid::None; }
    double share = n.pipes.empty() ? 0 : n.amount / double(n.pipes.size());
    for (int id : n.pipes) {
        machines_[size_t(id)]->fluidAmount = share;
        machines_[size_t(id)]->fluid = n.fluid;
    }
}

// Groups the pipes into networks (flood fill) and adds up their shares.
void World::rebuildFluidNets() {
    fluidNets_.clear();
    for (auto& mp : machines_)
        if (mp && mp->alive && mp->isPipe()) mp->fluidNet = -1;
    for (int start = 0; start < int(machines_.size()); ++start) {
        if (!machineAlive(start) || !machines_[size_t(start)]->isPipe() || machines_[size_t(start)]->fluidNet >= 0) continue;
        FluidNet net;
        int idx = int(fluidNets_.size());
        std::vector<int> todo{start};
        machines_[size_t(start)]->fluidNet = idx;
        std::vector<int> pumps;
        while (!todo.empty()) {
            int id = todo.back();
            todo.pop_back();
            Machine& p = *machines_[size_t(id)];
            net.pipes.push_back(id);
            net.amount += p.fluidAmount;
            if (p.fluid != Fluid::None && p.fluidAmount > 0 && net.fluid == Fluid::None) net.fluid = p.fluid;
            for (int i = 0; i < 4; ++i) {
                TileRef r = refAt(p.pos + dirVec(Dir(i)));
                if (r >= 0) continue;
                int nid = -r - 1;
                Machine& n = *machines_[size_t(nid)];
                if (n.type == MachineType::WaterPump) {
                    if (std::find(pumps.begin(), pumps.end(), nid) == pumps.end()) pumps.push_back(nid);
                } else if (n.isPipe() && n.fluidNet < 0) {
                    n.fluidNet = idx;
                    todo.push_back(nid);
                }
            }
        }
        net.pumps = pumps;
        net.amount = std::min(net.amount, net.capacity());
        fluidNets_.push_back(net);
        shareFluid(fluidNets_.back());
    }
}

void World::fluidTick() {
    const double perTick = PUMP_RATE / double(TICKS_PER_SECOND);
    for (auto& mp : machines_) {
        if (!mp || !mp->alive || mp->type != MachineType::WaterPump) continue;
        mp->fluid = Fluid::Water;
        mp->fluidAmount = std::min(PUMP_CAPACITY, mp->fluidAmount + perTick);
    }
    // pumps put their water straight into the networks they touch
    for (FluidNet& n : fluidNets_) {
        if (n.pumps.empty() || (n.fluid != Fluid::None && n.fluid != Fluid::Water)) continue;   // other fluid: the water stays in the pump
        bool changed = false;
        for (int pid : n.pumps) {
            Machine& p = *machines_[size_t(pid)];
            double move = std::min(p.fluidAmount, n.capacity() - n.amount);
            if (move <= 0) continue;
            p.fluidAmount -= move;
            n.amount += move;
            n.fluid = Fluid::Water;
            changed = true;
        }
        if (changed) shareFluid(n);
    }
}
