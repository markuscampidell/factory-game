// sim/Build.cpp - see Build.hpp. Placement rules are ported from
// belt_system.py / machine_system.py.
#include "Build.hpp"
#include <algorithm>
#include <unordered_set>

const ItemAmounts& beltBuildCost() {
    static const ItemAmounts cost = {{itemByStringId("iron_ingot"), 10}, {itemByStringId("copper_ingot"), 5}};
    return cost;
}

// ---------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------
void Builder::gatherOccupants(const std::vector<Vec2i>& cells, std::vector<int>& belts, std::vector<int>& machines) const {
    belts.clear();
    machines.clear();
    for (Vec2i c : cells) {
        int b = world.beltIdAt(c);
        if (b >= 0 && std::find(belts.begin(), belts.end(), b) == belts.end()) belts.push_back(b);
        int m = world.machineIdAt(c);
        if (m >= 0 && std::find(machines.begin(), machines.end(), m) == machines.end()) machines.push_back(m);
    }
}

bool Builder::applyRefunds(Inventory& inv, const std::vector<int>& belts, const std::vector<int>& machines) const {
    for (int id : belts) {
        const Belt& b = world.belt(id);
        if (b.item != NO_ITEM && !inv.tryAdd(b.item, 1)) return false;
        for (auto& p : beltBuildCost())
            if (!inv.tryAdd(p.first, p.second)) return false;
    }
    for (int id : machines)
        for (auto& p : world.machine(id).refundItems())
            if (!inv.tryAdd(p.first, p.second)) return false;
    return true;
}

ItemAmounts Builder::refundTotals(const std::vector<int>& belts, const std::vector<int>& machines) const {
    ItemAmounts total;
    for (int id : belts) {
        if (world.belt(id).item != NO_ITEM) addAmount(total, world.belt(id).item, 1);
        for (auto& c : beltBuildCost()) addAmount(total, c.first, c.second);
    }
    for (int id : machines)
        for (auto& p : world.machine(id).refundItems()) addAmount(total, p.first, p.second);
    return total;
}

// ---------------------------------------------------------------------------
// Placing anything (see PlaceRequest in Build.hpp)
// ---------------------------------------------------------------------------
static std::vector<Vec2i> footprint(Vec2i tl, int w, int h) {
    std::vector<Vec2i> cells;
    for (int dx = 0; dx < w; ++dx)
        for (int dy = 0; dy < h; ++dy) cells.push_back({tl.x + dx, tl.y + dy});
    return cells;
}

static const Recipe* recipeChoice(const Machine& m, const std::string& id) {
    if (id.empty()) return nullptr;
    if (const auto* choices = m.recipeChoices())
        for (const Recipe* r : *choices)
            if (r->id == id) return r;
    return nullptr;
}

// What a producing machine holds in its buffers (handed out on a recipe change).
static ItemAmounts bufferContents(const Machine& m) {
    ItemAmounts held;
    for (auto& b : m.inputs) for (auto& p : b.inv.contents()) addAmount(held, p.first, p.second);
    for (auto& b : m.outputs) for (auto& p : b.inv.contents()) addAmount(held, p.first, p.second);
    return held;
}

// The recipe an existing machine would switch to, or nullptr (no change).
static const Recipe* recipeChange(const Machine& m, const PlaceRequest& r) {
    const Recipe* want = recipeChoice(m, r.recipeId);
    return (want && want != m.recipe && m.type != MachineType::Miner) ? want : nullptr;
}

static ItemId firstOre(World& w, MachineType t, Vec2i tl) {
    const MachineSpec& s = machineSpec(t);
    ItemId ore = NO_ITEM;
    for (int dy = 0; dy < s.h && ore == NO_ITEM; ++dy)   // row by row (same order as Python)
        for (int dx = 0; dx < s.w && ore == NO_ITEM; ++dx) ore = w.oreAt({tl.x + dx, tl.y + dy});
    return ore;
}

PlacePlan Builder::planPlacement(const std::vector<PlaceRequest>& reqs, bool allowReplace, const std::set<int>* protectedIds) const {
    PlacePlan plan;
    World& w = const_cast<World&>(world);   // reading ore generates terrain; nothing else changes
    std::set<int> rmBelts, rmMachines;
    ItemAmounts cost, refunds;
    bool blocked = false, noOre = false, undiscovered = false;
    for (const PlaceRequest& r : reqs) {
        PlaceStatus st = PlaceStatus::Ok;
        int keep = -1;
        if (requireExplored) {   // checked first: reading ore below would explore the chunk
            const MachineSpec& s = machineSpec(r.type);
            int w = r.belt ? 1 : s.w, h = r.belt ? 1 : s.h;
            for (int dy = 0; dy < h && st == PlaceStatus::Ok; ++dy)
                for (int dx = 0; dx < w && st == PlaceStatus::Ok; ++dx)
                    if (!tileExplored(r.pos + Vec2i{dx, dy})) st = PlaceStatus::Undiscovered;
        }
        if (st == PlaceStatus::Undiscovered) {
            undiscovered = true;
            plan.each.push_back(st);
            plan.existing.push_back(-1);
            continue;
        }
        if (r.belt) {
            TileRef ref = world.refAt(r.pos);
            if (isBlockedByPlayer(r.pos)) st = PlaceStatus::Blocked;
            else if (ref > 0) {
                if (world.belt(ref - 1).dir == r.dir || allowReplace) keep = ref - 1;   // updated in place
                else st = PlaceStatus::Blocked;
            } else if (ref < 0 && !(allowReplace && !(protectedIds && protectedIds->count(-ref - 1)))) {
                st = PlaceStatus::Blocked;
            } else {   // a new belt (a machine in the way is removed first)
                if (ref < 0) rmMachines.insert(-ref - 1);
                for (auto& c : beltBuildCost()) addAmount(cost, c.first, c.second);
            }
        } else {
            const MachineSpec& s = machineSpec(r.type);
            auto cells = footprint(r.pos, s.w, s.h);
            std::vector<int> ob, om;
            gatherOccupants(cells, ob, om);
            bool byPlayer = false;
            for (Vec2i c : cells) byPlayer |= isBlockedByPlayer(c);
            bool same = ob.empty() && om.size() == 1 && world.machine(om[0]).type == r.type && world.machine(om[0]).pos == r.pos;
            if (byPlayer) st = PlaceStatus::Blocked;
            else if (same && (!world.machine(om[0]).isSplitter() || world.machine(om[0]).dir == r.dir || allowReplace)) {
                keep = om[0];
                if (recipeChange(world.machine(keep), r))
                    for (auto& p : bufferContents(world.machine(keep))) addAmount(refunds, p.first, p.second);
            } else if (!ob.empty() || !om.empty()) {
                bool prot = false;
                if (protectedIds) for (int m : om) prot |= protectedIds->count(m) > 0;
                if (!allowReplace || prot) st = PlaceStatus::Blocked;
                else { rmBelts.insert(ob.begin(), ob.end()); rmMachines.insert(om.begin(), om.end()); }
            }
            if (st == PlaceStatus::Ok && keep < 0) {
                if (r.type == MachineType::Miner && firstOre(w, r.type, r.pos) == NO_ITEM) st = PlaceStatus::NoOre;
                for (auto& c : s.cost) addAmount(cost, c.first, c.second);
            }
        }
        blocked |= st == PlaceStatus::Blocked;
        noOre |= st == PlaceStatus::NoOre;
        plan.each.push_back(st);
        plan.existing.push_back(keep);
    }
    plan.cost = cost;
    plan.removeBelts.assign(rmBelts.begin(), rmBelts.end());
    plan.removeMachines.assign(rmMachines.begin(), rmMachines.end());
    if (blocked) { plan.status = PlaceStatus::Blocked; return plan; }
    if (undiscovered) { plan.status = PlaceStatus::Undiscovered; return plan; }
    if (noOre) { plan.status = PlaceStatus::NoOre; return plan; }

    // simulate on a copy: refunds in, cost out - same order as executePlacement
    for (auto& p : refundTotals(plan.removeBelts, plan.removeMachines)) addAmount(plan.delta, p.first, p.second);
    for (auto& p : refunds) addAmount(plan.delta, p.first, p.second);
    Inventory scratch = player.inventory;
    for (auto& p : plan.delta)
        if (!scratch.tryAdd(p.first, p.second)) { plan.status = PlaceStatus::NoSpace; return plan; }
    if (!scratch.tryRemoveAll(cost)) { plan.status = PlaceStatus::NoFunds; return plan; }
    for (auto& c : cost) addAmount(plan.delta, c.first, -c.second);
    return plan;
}

std::vector<int> Builder::executePlacement(const std::vector<PlaceRequest>& reqs, const PlacePlan& plan) {
    // 1. everything given back (guaranteed to fit, see planPlacement)
    applyRefunds(player.inventory, plan.removeBelts, plan.removeMachines);
    for (size_t i = 0; i < reqs.size(); ++i) {
        if (reqs[i].belt || plan.existing[i] < 0) continue;
        Machine& m = world.machine(plan.existing[i]);
        if (recipeChange(m, reqs[i]))
            for (auto& p : bufferContents(m)) player.inventory.tryAdd(p.first, p.second);
    }
    for (int b : plan.removeBelts) world.removeBelt(b);
    for (int m : plan.removeMachines) world.removeMachine(m);
    // 2. pay, then build / update
    std::vector<int> ids;
    for (size_t i = 0; i < reqs.size(); ++i) {
        const PlaceRequest& r = reqs[i];
        int keep = plan.existing[i];
        if (r.belt) {
            int id = keep;
            if (id >= 0) {
                Belt& b = world.belt(id);   // kept: its item stays on it
                b.dir = r.dir;
            } else {
                player.inventory.tryRemoveAll(beltBuildCost());
                id = world.addBelt(r.pos, r.dir);
            }
            if (r.setFilters) world.belt(id).filter = r.beltFilter;
            ids.push_back(id);
            continue;
        }
        int id = keep;
        if (id >= 0) {
            Machine& m = world.machine(id);   // kept: contents stay unless the recipe changes
            if (m.isSplitter()) m.dir = r.dir;
            if (const Recipe* rc = recipeChange(m, r)) {
                m.recipe = rc;
                m.processing = false;
                m.timer = 0;
                m.resetBuffers();
            }
        } else {
            player.inventory.tryRemoveAll(machineSpec(r.type).cost);
            auto mach = makeMachine(r.type, r.pos, r.dir, r.type == MachineType::Miner ? firstOre(world, r.type, r.pos) : NO_ITEM);
            if (const Recipe* rc = recipeChoice(*mach, r.recipeId)) { mach->recipe = rc; mach->resetBuffers(); }
            id = world.addMachine(std::move(mach));
        }
        if (r.setFilters) world.machine(id).sideFilters = r.sideFilters;
        ids.push_back(id);
    }
    world.markTopologyDirty();
    world.ensureTopology();
    return ids;
}

// ---------------------------------------------------------------------------
// Machines
// ---------------------------------------------------------------------------
Vec2i Builder::machineTopLeft(MachineType t, Vec2i mouseTile) {
    const MachineSpec& s = machineSpec(t);
    return {mouseTile.x - s.w / 2, mouseTile.y - s.h / 2};
}


PlaceStatus Builder::checkMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, ItemAmounts* cost) const {
    PlaceRequest r;
    r.type = t;
    r.pos = machineTopLeft(t, mouseTile);
    r.dir = dir;
    PlacePlan plan = planPlacement({r}, allowReplace);
    if (cost) *cost = plan.cost;
    return plan.status;
}

int Builder::placeMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, const std::set<int>* protectedIds,
                          ItemAmounts* delta) {
    PlaceRequest r;
    r.type = t;
    r.pos = machineTopLeft(t, mouseTile);
    r.dir = dir;
    std::vector<PlaceRequest> reqs{r};
    PlacePlan plan = planPlacement(reqs, allowReplace, protectedIds);
    if (plan.status != PlaceStatus::Ok) return -1;
    if (delta) *delta = plan.delta;
    return executePlacement(reqs, plan)[0];   // new machine, or the identical one already there
}

bool Builder::canAffordMachineDeletion(int id) const {
    Inventory scratch = player.inventory;
    return applyRefunds(scratch, {}, {id});
}

bool Builder::deleteMachineAt(Vec2i t, ItemAmounts* delta) {
    int id = world.machineIdAt(t);
    if (id < 0 || !canAffordMachineDeletion(id)) return false;
    if (delta) *delta = refundTotals({}, {id});
    applyRefunds(player.inventory, {}, {id});
    world.removeMachine(id);
    return true;
}

bool Builder::setRecipe(Machine& m, const Recipe* r) {
    if (!m.isProducing() || m.type == MachineType::Miner) return false;
    if (m.recipe == r) return true;
    Inventory scratch = player.inventory;
    ItemAmounts held;
    for (auto& b : m.inputs) for (auto& p : b.inv.contents()) addAmount(held, p.first, p.second);
    for (auto& b : m.outputs) for (auto& p : b.inv.contents()) addAmount(held, p.first, p.second);
    for (auto& p : held)
        if (!scratch.tryAdd(p.first, p.second)) return false; // would lose items
    for (auto& p : held) player.inventory.tryAdd(p.first, p.second);
    m.recipe = r;
    m.processing = false;
    m.timer = 0;
    m.resetBuffers();
    world.markTopologyDirty();
    return true;
}

// ---------------------------------------------------------------------------
// Belts
// ---------------------------------------------------------------------------
std::vector<Vec2i> Builder::dragTiles(Vec2i start, Vec2i end, Dir placementDir) const {
    Vec2i dv = dirVec(placementDir);
    bool horizontalFirst = dv.x != 0;
    bool reversed = false;
    if (horizontalFirst && end.x != start.x) {
        int drag = end.x > start.x ? 1 : -1, face = dv.x > 0 ? 1 : -1;
        reversed = drag != face;
    } else if (!horizontalFirst && end.y != start.y) {
        int drag = end.y > start.y ? 1 : -1, face = dv.y > 0 ? 1 : -1;
        reversed = drag != face;
    }
    std::vector<Vec2i> tiles;
    int sx = end.x >= start.x ? 1 : -1;
    int sy = end.y >= start.y ? 1 : -1;
    if (horizontalFirst) {
        for (int x = start.x; x != end.x + sx; x += sx) tiles.push_back({x, start.y});
        for (int y = start.y + sy; y != end.y + sy; y += sy) tiles.push_back({end.x, y});
    } else {
        for (int y = start.y; y != end.y + sy; y += sy) tiles.push_back({start.x, y});
        for (int x = start.x + sx; x != end.x + sx; x += sx) tiles.push_back({x, end.y});
    }
    if (reversed) std::reverse(tiles.begin(), tiles.end());
    return tiles;
}

std::vector<BeltPlan> Builder::planBelts(Vec2i start, Vec2i end, Dir placementDir) const {
    auto tiles = dragTiles(start, end, placementDir);
    std::vector<BeltPlan> plan;
    for (size_t i = 0; i < tiles.size(); ++i) {
        Dir d = placementDir;
        if (tiles.size() > 1) {
            Vec2i v = i + 1 < tiles.size() ? tiles[i + 1] - tiles[i] : tiles[i] - tiles[i - 1];
            dirFromVec(v, d);
        }
        plan.push_back({tiles[i], d, std::nullopt});
    }
    for (size_t i = 1; i < plan.size(); ++i) plan[i].newIncoming = plan[i - 1].dir;
    return plan;
}

bool Builder::segmentMatchesExisting(const BeltPlan& p) const {
    int id = world.beltIdAt(p.pos);
    return id >= 0 && world.belt(id).dir == p.dir;
}

bool Builder::segmentReversesExisting(const BeltPlan& p) const {
    if (!p.newIncoming) return false;
    int id = world.beltIdAt(p.pos);
    if (id < 0) return false;
    const Belt& b = world.belt(id);
    uint8_t mask = b.incomingMask ? b.incomingMask : dirBit(b.dir);
    int count = 0;
    Dir only = b.dir;
    for (int i = 0; i < 4; ++i) if (mask & dirBit(Dir(i))) { ++count; only = Dir(i); }
    if (count != 1) return false;
    return p.dir == opposite(only) && *p.newIncoming == opposite(b.dir);
}

bool Builder::tileAutoReplaceable(const BeltPlan& p) const {
    return !world.isCellBlocked(p.pos) || segmentMatchesExisting(p) || segmentReversesExisting(p);
}

bool Builder::othersFreeOrMatching(const std::vector<BeltPlan>& plan, Vec2i start) const {
    for (auto& p : plan)
        if (p.pos != start && !tileAutoReplaceable(p)) return false;
    return true;
}

bool Builder::isDragTileBlocked(const BeltPlan& p, Vec2i start, bool othersOk, bool allowReplace) const {
    if (isBlockedByPlayer(p.pos)) return true;
    if (allowReplace) return false;
    if (!world.isCellBlocked(p.pos)) return false;
    if (p.pos == start) return !(world.beltIdAt(p.pos) >= 0 && othersOk);
    return !(segmentMatchesExisting(p) || segmentReversesExisting(p));
}

bool Builder::anyDragTileBlocked(const std::vector<BeltPlan>& plan, Vec2i start, bool allowReplace) const {
    bool othersOk = othersFreeOrMatching(plan, start);
    for (auto& p : plan)
        if (isDragTileBlocked(p, start, othersOk, allowReplace)) return true;
    return false;
}

// Belts already on the drag tiles are kept (turned, item stays) - only
// new belts cost something, machines in the way are refunded.
static std::vector<PlaceRequest> beltRequests(const std::vector<BeltPlan>& plan) {
    std::vector<PlaceRequest> reqs;
    for (auto& p : plan) {
        PlaceRequest r;
        r.belt = true;
        r.pos = p.pos;
        r.dir = p.dir;
        reqs.push_back(r);
    }
    return reqs;
}

PlaceStatus Builder::beltAffordability(const std::vector<BeltPlan>& plan, ItemAmounts* cost) const {
    // blocking is checked separately (drag rules); here only items matter
    PlacePlan pp = planPlacement(beltRequests(plan), true);
    if (cost) *cost = pp.cost;
    PlaceStatus st = pp.status;
    return (st == PlaceStatus::NoSpace || st == PlaceStatus::NoFunds) ? st : PlaceStatus::Ok;
}

std::vector<bool> Builder::runningAffordability(const std::vector<BeltPlan>& plan) const {
    Inventory scratch = player.inventory;
    std::unordered_set<int> seenMachines;
    std::vector<bool> flags;
    for (auto& p : plan) {
        if (world.beltIdAt(p.pos) >= 0) { flags.push_back(true); continue; }   // kept, free
        std::vector<int> rb, rm, rm2;
        gatherOccupants({p.pos}, rb, rm);
        for (int m : rm) if (!seenMachines.count(m)) { rm2.push_back(m); seenMachines.insert(m); }
        bool ok = applyRefunds(scratch, {}, rm2) && scratch.tryRemoveAll(beltBuildCost());
        flags.push_back(ok);
    }
    return flags;
}

bool Builder::placeBelts(Vec2i start, Vec2i end, Dir placementDir, bool allowReplace, ItemAmounts* delta) {
    world.ensureTopology();
    auto plan = planBelts(start, end, placementDir);
    if (plan.empty() || anyDragTileBlocked(plan, start, allowReplace)) return false;
    // the drag rules above decided which belts may be turned; machines only with Shift
    auto reqs = beltRequests(plan);
    PlacePlan pp = planPlacement(reqs, true);
    if (pp.status != PlaceStatus::Ok) return false;
    if (delta) *delta = pp.delta;
    executePlacement(reqs, pp);
    return true;
}

std::vector<int> Builder::connectedBelts(int beltId) const {
    std::vector<int> result;
    std::unordered_set<int> visited;
    std::vector<int> stack{beltId};
    while (!stack.empty()) {
        int id = stack.back();
        stack.pop_back();
        if (!visited.insert(id).second) continue;
        result.push_back(id);
        const Belt& a = world.belt(id);
        for (int i = 0; i < 4; ++i) {
            Vec2i n = a.pos + dirVec(Dir(i));
            int nid = world.beltIdAt(n);
            if (nid < 0 || visited.count(nid)) continue;
            const Belt& b = world.belt(nid);
            bool aToB = a.pos + dirVec(a.dir) == b.pos;
            bool bToA = b.pos + dirVec(b.dir) == a.pos;
            if (aToB && bToA) continue; // head-on: not connected
            if (aToB || bToA) stack.push_back(nid);
        }
    }
    return result;
}

bool Builder::canAffordBeltDeletion(const std::vector<int>& ids) const {
    Inventory scratch = player.inventory;
    return applyRefunds(scratch, ids, {});
}

bool Builder::deleteBeltAt(Vec2i t, bool whole, ItemAmounts* delta) {
    int id = world.beltIdAt(t);
    if (id < 0) return false;
    std::vector<int> ids = whole ? connectedBelts(id) : std::vector<int>{id};
    if (!canAffordBeltDeletion(ids)) return false;
    if (delta) *delta = refundTotals(ids, {});
    applyRefunds(player.inventory, ids, {});
    for (int b : ids) world.removeBelt(b);
    return true;
}

// ---------------------------------------------------------------------------
// Blueprints
// ---------------------------------------------------------------------------
static std::vector<PlaceRequest> blueprintRequests(const Blueprint& bp, Vec2i tl) {
    std::vector<PlaceRequest> reqs;
    for (auto& b : bp.belts) {
        PlaceRequest r;
        r.belt = true;
        r.pos = tl + b.off;
        r.dir = b.dir;
        r.setFilters = true;
        r.beltFilter = b.filter;
        reqs.push_back(r);
    }
    for (auto& m : bp.machines) {
        PlaceRequest r;
        r.type = m.type;
        r.pos = tl + m.off;
        r.dir = m.dir;
        r.recipeId = m.recipeId;
        r.setFilters = true;
        r.sideFilters = m.sideFilters;
        reqs.push_back(r);
    }
    return reqs;
}

Builder::BlueprintCheck Builder::checkBlueprint(const Blueprint& bp, Vec2i tl, bool allowReplace) const {
    PlacePlan plan = planPlacement(blueprintRequests(bp, tl), allowReplace);
    BlueprintCheck c;
    c.status = plan.status;
    c.cost = plan.cost;
    c.belts.assign(plan.each.begin(), plan.each.begin() + long(bp.belts.size()));
    c.machines.assign(plan.each.begin() + long(bp.belts.size()), plan.each.end());
    return c;
}

bool Builder::placeBlueprint(const Blueprint& bp, Vec2i tl, bool allowReplace, ItemAmounts* delta) {
    if (bp.empty()) return false;
    auto reqs = blueprintRequests(bp, tl);
    PlacePlan plan = planPlacement(reqs, allowReplace);
    if (plan.status != PlaceStatus::Ok) return false;   // all or nothing
    if (delta) *delta = plan.delta;
    executePlacement(reqs, plan);
    return true;
}

// ---------------------------------------------------------------------------
// Area delete
// ---------------------------------------------------------------------------
Builder::AreaDelete Builder::deleteArea(Vec2i a, Vec2i b, ItemAmounts* refunded, int* removed) {
    std::vector<int> belts, machines;
    gatherSelection(world, makeSelection(a, b), belts, machines);   // same rules as copying
    if (belts.empty() && machines.empty()) return AreaDelete::Nothing;

    Inventory scratch = player.inventory;
    if (!applyRefunds(scratch, belts, machines)) return AreaDelete::NoSpace;

    if (refunded) *refunded = refundTotals(belts, machines);
    if (removed) *removed = int(belts.size() + machines.size());
    applyRefunds(player.inventory, belts, machines);
    for (int id : belts) world.removeBelt(id);
    for (int id : machines) world.removeMachine(id);
    return AreaDelete::Ok;
}
