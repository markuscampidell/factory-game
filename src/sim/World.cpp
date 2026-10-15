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
        {"water_pump", "Water Pump", 2, 1, "assets/sprites/machines/water_pump.png", {{iron, 5}, {copper, 3}}},
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

void World::removeBelt(int id) {
    Belt& b = belts_[size_t(id)];
    if (!b.alive) return;
    if (recordChanges) recordRemoved(beltSnap(b));
    setRef(b.pos, 0);
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
uint8_t World::computeIncoming(Vec2i pos, Dir dir, const Overlay* ov) const {
    uint8_t mask = 0;
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
        TileRef r = refAt(src);
        if (r > 0 && !removed) {              // a belt feeds us if it points here
            if (belts_[size_t(r - 1)].dir == travel) mask |= dirBit(travel);
            continue;
        }
        if (ov && ov->hasSplitter && ov->splitterPos == src) {
            if (travel != opposite(ov->splitterDir)) mask |= dirBit(travel);
            continue;
        }
        if (r < 0) {
            int mid = -r - 1;
            if (ov && ov->ignoreMachine == mid) continue;
            const Machine& m = *machines_[size_t(mid)];
            if (m.isSplitter()) {
                if (travel != opposite(m.dir)) mask |= dirBit(travel);
            } else if (m.isStorage()) {
                // a chest only pushes onto a belt pointing straight away
                if (travel == dir) mask |= dirBit(travel);
            } else if (isOutputSide(m.type, m.dir, travel)) {
                // a belt on an output side of a machine (pointing away or
                // running past it, joined like an intersection)
                mask |= dirBit(travel);
            }
        }
    }
    return mask;
}

void World::ensureTopology() {
    if (!topologyDirty_) return;
    // 1) neighbours that don't depend on the incoming masks
    splitterIds_.clear();
    for (auto& b : belts_)
        if (b.alive) b.frontRef = refAt(b.pos + dirVec(b.dir));
    for (int mid = 0; mid < int(machines_.size()); ++mid) {
        if (!machineAlive(mid) || !machines_[size_t(mid)]->isSplitter()) continue;
        Machine& m = *machines_[size_t(mid)];
        splitterIds_.push_back(mid);
        for (int i = 0; i < 3; ++i) m.sideRef[size_t(i)] = refAt(m.pos + dirVec(m.sideDir(i)));
        m.backRef = refAt(m.pos - dirVec(m.dir));
    }
    // 2) incoming masks
    for (auto& b : belts_) {
        if (!b.alive) continue;
        uint8_t old = b.incomingMask;
        b.incomingMask = computeIncoming(b.pos, b.dir);
        if (old != b.incomingMask && !(b.incomingMask & dirBit(Dir(b.rr % 4)))) {
            // keep favouring the same side if it still exists, else restart
            b.rr = 0;
        }
        for (int i = 0; i < 4; ++i)
            b.feedRef[size_t(i)] = (b.incomingMask & dirBit(Dir(i))) ? refAt(b.pos - dirVec(Dir(i))) : 0;
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
        });
    }
    rebuildTransportOrder();
    topologyDirty_ = false;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
void World::tick() {
    ensureTopology();
    transportTick();
    machinesTick();
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
