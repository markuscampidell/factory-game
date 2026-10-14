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

Dir normalizeMachineDir(MachineType t, Dir d) {
    if (t == MachineType::Splitter) return d;
    const MachineSpec& s = machineSpec(t);
    if (s.w == s.h) return Dir::Right;
    return (d == Dir::Down || d == Dir::Up) ? Dir::Down : Dir::Right;
}

bool machineTurnable(MachineType t) { return t == MachineType::Splitter || machineSpec(t).w != machineSpec(t).h; }

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
    belts_.clear();
    freeBelts_.clear();
    machines_.clear();
    freeMachines_.clear();
    topologyDirty_ = true;
    linesReady_ = false;
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
    linesReady_ = false;
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
    linesReady_ = false;
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
    linesReady_ = false;
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
    linesReady_ = false;
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
            } else if (travel == dir) {
                // a belt pointing straight away from any machine
                mask |= dirBit(travel);
            } else if (m.isProducing() && !lineFeedsMachine(pos, mid, ov)) {
                // a belt running past a producing machine's side, joined
                // like an intersection - unless it is the machine's own
                // input line
                mask |= dirBit(travel);
            }
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------
// Belt lines
// ---------------------------------------------------------------------------
namespace {
// What is at tile t as far as belt lines care, "what if" overlay included.
struct LineTile {
    enum Kind : uint8_t { None, BeltTile, SplitterTile, MachineTile } kind = None;
    Dir dir = Dir::Right;
    int mid = -1;   // machine id (splitters and machines)
};

LineTile lineTileAt(const World& w, Vec2i t, const World::Overlay* ov) {
    LineTile lt;
    if (ov) {
        auto it = ov->belts.find(t);
        if (it != ov->belts.end()) { lt.kind = LineTile::BeltTile; lt.dir = it->second; return lt; }
        if (ov->removed.count(t)) return lt;
        if (ov->hasSplitter && ov->splitterPos == t) { lt.kind = LineTile::SplitterTile; lt.dir = ov->splitterDir; return lt; }
    }
    TileRef r = w.refAt(t);
    if (r > 0) { lt.kind = LineTile::BeltTile; lt.dir = w.belt(r - 1).dir; return lt; }
    if (r < 0) {
        int mid = -r - 1;
        if (ov && ov->ignoreMachine == mid) return lt;
        const Machine& m = w.machine(mid);
        lt.kind = m.isSplitter() ? LineTile::SplitterTile : LineTile::MachineTile;
        lt.dir = m.dir;
        lt.mid = mid;
    }
    return lt;
}

// Where a belt / splitter at p sends items: onNode(q) for each belt or
// splitter it hands items to (same rules as the transport graph), onFeed(id)
// for each machine it puts items into.
template <class OnNode, class OnFeed>
void lineForward(const World& w, Vec2i p, const LineTile& lt, const World::Overlay* ov, OnNode onNode, OnFeed onFeed) {
    auto target = [&](Dir d) {
        Vec2i q = p + dirVec(d);
        LineTile qt = lineTileAt(w, q, ov);
        if (qt.kind == LineTile::BeltTile) { if (qt.dir != opposite(d)) onNode(q); }
        else if (qt.kind == LineTile::SplitterTile) { if (qt.dir == d) onNode(q); }
        else if (qt.kind == LineTile::MachineTile) onFeed(qt.mid);
    };
    if (lt.kind == LineTile::BeltTile) target(lt.dir);
    else if (lt.kind == LineTile::SplitterTile)
        for (int i = 0; i < 3; ++i) target(rotateCW(lt.dir, i - 1));
}

// Walks the whole line through `start` (downstream and upstream) with a
// "what if" overlay - for build previews; the real lines are cached by
// rebuildLines(). Calls onFeed(machine id) for every machine the line feeds
// and stops as soon as it returns true (then returns true).
template <class OnFeed>
bool walkLine(const World& w, Vec2i start, const World::Overlay* ov, OnFeed onFeed) {
    std::unordered_set<Vec2i, Vec2iHash> seen{start};
    std::vector<Vec2i> todo{start};
    bool stop = false;
    while (!todo.empty() && !stop) {
        Vec2i p = todo.back();
        todo.pop_back();
        LineTile lt = lineTileAt(w, p, ov);
        auto visit = [&](Vec2i q) { if (seen.insert(q).second) todo.push_back(q); };
        // downstream
        lineForward(w, p, lt, ov, visit, [&](int mid) { if (onFeed(mid)) stop = true; });
        // upstream: neighbours that send items to p
        for (int i = 0; i < 4; ++i) {
            Vec2i q = p + dirVec(Dir(i));
            if (seen.count(q)) continue;
            LineTile qt = lineTileAt(w, q, ov);
            if (qt.kind != LineTile::BeltTile && qt.kind != LineTile::SplitterTile) continue;
            bool sendsHere = false;
            lineForward(w, q, qt, ov, [&](Vec2i x) { if (x == p) sendsHere = true; }, [](int) {});
            if (sendsHere) visit(q);
        }
    }
    return stop;
}
} // namespace

// Needs the cached neighbours (belt frontRef, splitter sideRef). Belts and
// splitters that hand items to each other are joined (union-find), so every
// line ends up as one set - the same lines walkLine() finds.
void World::rebuildLines() {
    const int B = int(belts_.size());
    const int N = B + int(machines_.size());   // nodes: belts, then B + machine id
    std::vector<int> parent(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) parent[size_t(i)] = i;
    auto find = [&](int x) {
        while (parent[size_t(x)] != x) { parent[size_t(x)] = parent[size_t(parent[size_t(x)])]; x = parent[size_t(x)]; }
        return x;
    };
    auto join = [&](int a, int b) { a = find(a); b = find(b); if (a != b) parent[size_t(a)] = b; };
    std::vector<std::pair<int, int>> feeds;   // (node, machine id it puts items into)
    // node `from` sends items in direction d to the tile referenced by r
    auto link = [&](int from, Dir d, TileRef r) {
        if (r > 0) {
            if (d != opposite(belts_[size_t(r - 1)].dir)) join(from, r - 1);
        } else if (r < 0) {
            const Machine& m = *machines_[size_t(-r - 1)];
            if (!m.isSplitter()) feeds.push_back({from, -r - 1});
            else if (m.dir == d) join(from, B + (-r - 1));
        }
    };
    for (int i = 0; i < B; ++i)
        if (belts_[size_t(i)].alive) link(i, belts_[size_t(i)].dir, belts_[size_t(i)].frontRef);
    for (int mid : splitterIds_) {
        const Machine& s = *machines_[size_t(mid)];
        for (int k = 0; k < 3; ++k) link(B + mid, s.sideDir(k), s.sideRef[size_t(k)]);
    }
    beltLine_.assign(size_t(B), -1);
    for (int i = 0; i < B; ++i) if (belts_[size_t(i)].alive) beltLine_[size_t(i)] = find(i);
    lineFeeds_.clear();
    for (auto& f : feeds) lineFeeds_.insert((uint64_t(uint32_t(find(f.first))) << 32) | uint32_t(f.second));
    linesReady_ = true;
}

bool World::lineFeedsMachine(Vec2i beltPos, int machineId, const Overlay* ov) const {
    if (!ov && linesReady_) {
        int id = beltIdAt(beltPos);
        if (id < 0 || size_t(id) >= beltLine_.size() || beltLine_[size_t(id)] < 0) return false;
        return lineFeeds_.count((uint64_t(uint32_t(beltLine_[size_t(id)])) << 32) | uint32_t(machineId)) != 0;
    }
    // previews (or topology out of date): walk the line
    return walkLine(*this, beltPos, ov, [&](int mid) { return mid == machineId; });
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
    // 2) belt lines (side outputs need them), 3) incoming masks
    rebuildLines();
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
    // 4) what each machine pushes onto (computeIncoming decided that)
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
