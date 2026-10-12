// sim/World.cpp - world storage, machines, and belt topology.
#include "World.hpp"
#include "Terrain.hpp"

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

std::unique_ptr<Machine> makeMachine(MachineType type, Vec2i topLeft, Dir dir, ItemId ore) {
    auto m = std::make_unique<Machine>();
    const MachineSpec& s = machineSpec(type);
    m->type = type;
    m->pos = topLeft;
    m->w = s.w;
    m->h = s.h;
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
    for (int dy = 0; dy < m.h; ++dy) {
        tiles.push_back({{m.pos.x + m.w, m.pos.y + dy}, Dir::Right});
        tiles.push_back({{m.pos.x - 1, m.pos.y + dy}, Dir::Left});
    }
    for (int dx = 0; dx < m.w; ++dx) {
        tiles.push_back({{m.pos.x + dx, m.pos.y - 1}, Dir::Up});
        tiles.push_back({{m.pos.x + dx, m.pos.y + m.h}, Dir::Down});
    }
    return tiles;
}

// ---------------------------------------------------------------------------
// World: chunks & tiles
// ---------------------------------------------------------------------------
void World::clear() {
    chunks_.clear();
    explored_.clear();
    belts_.clear();
    freeBelts_.clear();
    machines_.clear();
    freeMachines_.clear();
    topologyDirty_ = true;
    tickCount_ = 0;
    terrain::clearCache();
}

Chunk* World::chunk(int cx, int cy, bool create) {
    Vec2i key{cx, cy};
    auto it = chunks_.find(key);
    if (it != chunks_.end()) return it->second.get();
    if (!create) return nullptr;
    auto c = std::make_unique<Chunk>();
    Chunk* raw = c.get();
    chunks_.emplace(key, std::move(c));
    return raw;
}

TileRef World::refAt(Vec2i t) const {
    auto it = chunks_.find(Vec2i{floorDiv(t.x, CHUNK_SIZE), floorDiv(t.y, CHUNK_SIZE)});
    if (it == chunks_.end()) return 0;
    return it->second->refs[size_t(floorMod(t.y, CHUNK_SIZE) * CHUNK_SIZE + floorMod(t.x, CHUNK_SIZE))];
}

void World::setRef(Vec2i t, TileRef r) {
    Chunk* c = chunk(floorDiv(t.x, CHUNK_SIZE), floorDiv(t.y, CHUNK_SIZE), true);
    TileRef& slot = c->refs[size_t(floorMod(t.y, CHUNK_SIZE) * CHUNK_SIZE + floorMod(t.x, CHUNK_SIZE))];
    if (slot == 0 && r != 0) c->occupants++;
    if (slot != 0 && r == 0) c->occupants--;
    slot = r;
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
    int cx = floorDiv(t.x, CHUNK_SIZE), cy = floorDiv(t.y, CHUNK_SIZE);
    Chunk* c = chunk(cx, cy, true);
    ensureTerrain(*c, cx, cy);
    return c->terrain[size_t(floorMod(t.y, CHUNK_SIZE) * CHUNK_SIZE + floorMod(t.x, CHUNK_SIZE))];
}

ItemId World::oreAt(Vec2i t) { return terrain::oreForTileType(tileType(t)); }

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
    return id;
}

void World::removeBelt(int id) {
    Belt& b = belts_[size_t(id)];
    if (!b.alive) return;
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
    return id;
}

void World::removeMachine(int id) {
    if (!machineAlive(id)) return;
    Machine& m = *machines_[size_t(id)];
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

    auto beltDir = [&](Vec2i t, Dir& out) -> bool {
        if (ov) {
            auto it = ov->belts.find(t);
            if (it != ov->belts.end()) { out = it->second; return true; }
            if (ov->removed.count(t)) return false;
        }
        TileRef r = refAt(t);
        if (r > 0) { out = belts_[size_t(r - 1)].dir; return true; }
        return false;
    };

    for (int i = 0; i < 4; ++i) {
        Dir travel = Dir(i);                  // direction items would move
        Vec2i src = pos - dirVec(travel);     // tile they would come from
        if (travel == opposite(dir)) continue; // never from straight ahead

        Dir nd;
        if (beltDir(src, nd)) {
            if (nd == travel) mask |= dirBit(travel);
            continue;
        }
        if (ov && ov->belts.count(src)) continue;
        if (ov && ov->hasSplitter && ov->splitterPos == src) {
            if (travel != opposite(ov->splitterDir)) mask |= dirBit(travel);
            continue;
        }
        TileRef r = refAt(src);
        if (r < 0) {
            int mid = -r - 1;
            if (ov && ov->ignoreMachine == mid) continue;
            const Machine& m = *machines_[size_t(mid)];
            if (m.isSplitter()) {
                if (travel != opposite(m.dir)) mask |= dirBit(travel);
            } else {
                // Machines push straight out of their side, so they only
                // feed a belt that points directly away from them.
                if (travel == dir) mask |= dirBit(travel);
            }
        }
    }
    return mask;
}

void World::ensureTopology() {
    if (!topologyDirty_) return;
    for (auto& b : belts_) {
        if (!b.alive) continue;
        uint8_t old = b.incomingMask;
        b.incomingMask = computeIncoming(b.pos, b.dir);
        if (old != b.incomingMask && !(b.incomingMask & dirBit(Dir(b.rr % 4)))) {
            // keep favouring the same side if it still exists, else restart
            b.rr = 0;
        }
    }
    // cache neighbour lookups for the transport hot loop
    for (auto& b : belts_) {
        if (!b.alive) continue;
        b.frontRef = refAt(b.pos + dirVec(b.dir));
        for (int i = 0; i < 4; ++i)
            b.feedRef[size_t(i)] = (b.incomingMask & dirBit(Dir(i))) ? refAt(b.pos - dirVec(Dir(i))) : 0;
    }
    for (auto& mp : machines_) {
        if (!mp || !mp->alive) continue;
        Machine& m = *mp;
        m.outBelts.clear();
        if (m.isSplitter()) {
            for (int i = 0; i < 3; ++i) m.sideRef[size_t(i)] = refAt(m.pos + dirVec(m.sideDir(i)));
            m.backRef = refAt(m.pos - dirVec(m.dir));
        } else {
            for (const OutputTile& ot : machineOutputTiles(m)) {
                TileRef r = refAt(ot.tile);
                if (r > 0 && belts_[size_t(r - 1)].dir == ot.dir) m.outBelts.push_back(r - 1);
            }
        }
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
