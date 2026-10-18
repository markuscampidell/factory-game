// sim/Build.cpp - see Build.hpp. Placement rules are ported from
// belt_system.py / machine_system.py.
#include "Build.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

const ItemAmounts& beltBuildCost() {
    static const ItemAmounts cost = {{itemByStringId("iron_ingot"), 10}, {itemByStringId("copper_ingot"), 5}};
    return cost;
}

const ItemAmounts& bridgeBuildCost() {
    static const ItemAmounts cost = {{itemByStringId("iron_ingot"), 20}, {itemByStringId("copper_ingot"), 10}};
    return cost;
}

// ---------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------
// A pipe at t would join whatever fluids its neighbours hold (pipes: their
// network's fluid, water pumps: water) - more than one kind can't be mixed.
bool pipeWouldMixFluids(const World& w, Vec2i t) {
    Fluid seen = Fluid::None;
    for (int i = 0; i < 4; ++i) {
        TileRef r = w.refAt(t + dirVec(Dir(i)));
        if (r >= 0) continue;
        const Machine& m = w.machine(-r - 1);
        Fluid f = m.type == MachineType::WaterPump ? Fluid::Water : m.isPipe() ? m.fluid : Fluid::None;
        if (f == Fluid::None) continue;
        if (seen != Fluid::None && seen != f) return true;
        seen = f;
    }
    return false;
}

bool touchesWater(World& w, MachineType t, Vec2i tl, Dir d) {
    Vec2i s = machineSize(t, d);
    for (int i = 0; i < s.x; ++i)
        if (w.isWater({tl.x + i, tl.y - 1}) || w.isWater({tl.x + i, tl.y + s.y})) return true;
    for (int i = 0; i < s.y; ++i)
        if (w.isWater({tl.x - 1, tl.y + i}) || w.isWater({tl.x + s.x, tl.y + i})) return true;
    return false;
}

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
        for (auto& p : beltCost(b.bridge))
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
        for (auto& c : beltCost(world.belt(id).bridge)) addAmount(total, c.first, c.second);
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

static ItemId firstOre(World& w, MachineType t, Vec2i tl, Dir d) {
    Vec2i s = machineSize(t, d);
    ItemId ore = NO_ITEM;
    for (int dy = 0; dy < s.y && ore == NO_ITEM; ++dy)   // row by row (same order as Python)
        for (int dx = 0; dx < s.x && ore == NO_ITEM; ++dx) ore = w.oreAt({tl.x + dx, tl.y + dy});
    return ore;
}

Vec2i Builder::playerChunk() const {
    int tx = int(std::floor(player.rect.cx() / CELL_SIZE)), ty = int(std::floor(player.rect.cy() / CELL_SIZE));
    return {floorDiv(tx, CHUNK_SIZE), floorDiv(ty, CHUNK_SIZE)};
}

void Builder::reachTiles(int& x0, int& y0, int& x1, int& y1) const {
    Vec2i c = playerChunk();
    x0 = (c.x - blueprintReach) * CHUNK_SIZE;
    y0 = (c.y - blueprintReach) * CHUNK_SIZE;
    x1 = (c.x + blueprintReach + 1) * CHUNK_SIZE - 1;
    y1 = (c.y + blueprintReach + 1) * CHUNK_SIZE - 1;
}

bool Builder::inBlueprintReach(Vec2i t) const {
    if (blueprintReach < 0) return true;
    int x0, y0, x1, y1;
    reachTiles(x0, y0, x1, y1);
    return t.x >= x0 && t.x <= x1 && t.y >= y0 && t.y <= y1;
}

PlacePlan Builder::planPlacement(const std::vector<PlaceRequest>& reqs, bool allowReplace, const std::set<int>* protectedIds,
                                 bool limitReach, const std::vector<Vec2i>* fill) const {
    PlacePlan plan;
    World& w = const_cast<World&>(world);   // reading ore generates terrain; nothing else changes
    std::set<int> rmBelts, rmMachines;
    ItemAmounts cost, refunds;
    bool blocked = false, noOre = false, undiscovered = false, outOfReach = false, onWater = false, needsWater = false, fluidMix = false;
    // landfill that comes with it (blueprints): the water tiles get filled first
    std::unordered_set<Vec2i, Vec2iHash> filled;
    if (fill)
        for (Vec2i t : *fill) {
            if (limitReach && blueprintReach >= 0 && !inBlueprintReach(t)) { outOfReach = true; continue; }
            if (requireExplored && !tileExplored(t)) { undiscovered = true; continue; }   // (before reading the terrain)
            if (!w.isWater(t) || !filled.insert(t).second) continue;
            plan.fill.push_back(t);
            for (auto& c : landfillTileCost()) addAmount(cost, c.first, c.second);
        }
    auto wet = [&](Vec2i t) { return !filled.count(t) && w.isWater(t); };
    for (const PlaceRequest& r : reqs) {
        PlaceStatus st = PlaceStatus::Ok;
        int keep = -1;
        if (limitReach && blueprintReach >= 0) {   // before anything that reads the world
            Vec2i sz = machineSize(r.type, r.dir);
            int w = r.belt ? 1 : sz.x, h = r.belt ? 1 : sz.y;
            if (!inBlueprintReach(r.pos) || !inBlueprintReach(r.pos + Vec2i{w - 1, h - 1})) st = PlaceStatus::OutOfReach;
        }
        if (st == PlaceStatus::OutOfReach) {
            outOfReach = true;
            plan.each.push_back(st);
            plan.existing.push_back(-1);
            continue;
        }
        if (requireExplored) {   // checked first: reading ore below would explore the chunk
            Vec2i sz = machineSize(r.type, r.dir);
            int w = r.belt ? 1 : sz.x, h = r.belt ? 1 : sz.y;
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
        if (r.belt && r.bridge) {
            // bridge layer: anywhere (water too) except on machines / splitters
            TileRef ref = world.refAt(r.pos);
            int there = world.bridgeIdAt(r.pos);
            bool prot = ref < 0 && protectedIds && protectedIds->count(-ref - 1);
            if (ref < 0 && (!allowReplace || prot)) st = PlaceStatus::Blocked;
            else if (there >= 0) keep = there;   // kept, or turned in place (no Shift needed) - if it still holds up (below)
            if (st == PlaceStatus::Ok) {
                if (ref < 0) rmMachines.insert(-ref - 1);   // (Shift) the machine goes
                if (keep < 0) for (auto& c : bridgeBuildCost()) addAmount(cost, c.first, c.second);
            }
        } else if (r.belt) {
            TileRef ref = world.refAt(r.pos);
            if (isBlockedByPlayer(r.pos)) st = PlaceStatus::Blocked;
            else if (ref > 0) {
                if (world.belt(ref - 1).dir == r.dir || allowReplace) keep = ref - 1;   // updated in place
                else st = PlaceStatus::Blocked;
            } else if (wet(r.pos)) {
                st = PlaceStatus::OnWater;
            } else if (ref < 0 && !(allowReplace && !(protectedIds && protectedIds->count(-ref - 1)))) {
                st = PlaceStatus::Blocked;
            } else {   // a new belt (a machine in the way is removed first)
                if (ref < 0) rmMachines.insert(-ref - 1);
                for (auto& c : beltBuildCost()) addAmount(cost, c.first, c.second);
            }
        } else {
            const MachineSpec& s = machineSpec(r.type);
            Vec2i sz = machineSize(r.type, r.dir);
            auto cells = footprint(r.pos, sz.x, sz.y);
            std::vector<int> ob, om;
            gatherOccupants(cells, ob, om);
            std::vector<int> bridgesOver;   // nothing stands under a bridge but belts
            for (Vec2i c : cells)
                if (int bid = world.bridgeIdAt(c); bid >= 0) bridgesOver.push_back(bid);
            bool byPlayer = false;
            for (Vec2i c : cells) byPlayer |= isBlockedByPlayer(c);
            // (a turned miner has another footprint: not the same machine)
            bool same = ob.empty() && om.size() == 1 && world.machine(om[0]).type == r.type && world.machine(om[0]).pos == r.pos &&
                        world.machine(om[0]).w == sz.x && world.machine(om[0]).h == sz.y;
            if (byPlayer) st = PlaceStatus::Blocked;
            else if (!bridgesOver.empty() && !allowReplace) st = PlaceStatus::Blocked;
            else if (same) {   // the same machine: kept, or turned in place (no Shift needed)
                keep = om[0];
                if (recipeChange(world.machine(keep), r))
                    for (auto& p : bufferContents(world.machine(keep))) addAmount(refunds, p.first, p.second);
            } else if (!ob.empty() || !om.empty()) {
                bool prot = false;
                if (protectedIds) for (int m : om) prot |= protectedIds->count(m) > 0;
                if (!allowReplace || prot) st = PlaceStatus::Blocked;
                else { rmBelts.insert(ob.begin(), ob.end()); rmMachines.insert(om.begin(), om.end()); }
            }
            if (st == PlaceStatus::Ok) rmBelts.insert(bridgesOver.begin(), bridgesOver.end());   // (Shift)
            if (st == PlaceStatus::Ok && keep < 0) {
                for (Vec2i c : cells)
                    if (wet(c)) { st = PlaceStatus::OnWater; break; }
                if (st == PlaceStatus::Ok && r.type == MachineType::WaterPump && !touchesWater(w, r.type, r.pos, r.dir)) st = PlaceStatus::NeedsWater;
                if (st == PlaceStatus::Ok && r.type == MachineType::Pipe && pipeWouldMixFluids(world, r.pos)) st = PlaceStatus::FluidMix;
            }
            if (st == PlaceStatus::Ok && keep < 0) {
                if (r.type == MachineType::Miner && firstOre(w, r.type, r.pos, r.dir) == NO_ITEM) st = PlaceStatus::NoOre;
                for (auto& c : s.cost) addAmount(cost, c.first, c.second);
            }
        }
        blocked |= st == PlaceStatus::Blocked;
        noOre |= st == PlaceStatus::NoOre;
        onWater |= st == PlaceStatus::OnWater;
        needsWater |= st == PlaceStatus::NeedsWater;
        fluidMix |= st == PlaceStatus::FluidMix;
        plan.each.push_back(st);
        plan.existing.push_back(keep);
    }
    // bridges: at most BRIDGE_MAX_LENGTH tiles in a row, counting the ones
    // already there (minus what goes) and the new / turned ones
    bool tooLong = false;
    {
        std::unordered_map<Vec2i, Dir, Vec2iHash> layer;
        bool anyBridge = false;
        for (size_t i = 0; i < reqs.size(); ++i) anyBridge |= reqs[i].bridge && plan.each[i] == PlaceStatus::Ok;
        if (anyBridge) {
            for (auto& kv : world.bridges())
                if (!rmBelts.count(kv.second)) layer[kv.first] = world.belt(kv.second).dir;
            for (size_t i = 0; i < reqs.size(); ++i)
                if (reqs[i].bridge && plan.each[i] == PlaceStatus::Ok) layer[reqs[i].pos] = reqs[i].dir;
            auto pointing = [&](Vec2i p, Dir d) { auto it = layer.find(p); return it != layer.end() && it->second == d; };
            for (size_t i = 0; i < reqs.size(); ++i) {
                if (!reqs[i].bridge || plan.each[i] != PlaceStatus::Ok) continue;
                Dir d = reqs[i].dir;
                int n = 1;
                for (Vec2i p = reqs[i].pos - dirVec(d); pointing(p, d) && n <= BRIDGE_MAX_LENGTH; p = p - dirVec(d)) ++n;
                for (Vec2i p = reqs[i].pos + dirVec(d); pointing(p, d) && n <= BRIDGE_MAX_LENGTH; p = p + dirVec(d)) ++n;
                if (n > BRIDGE_MAX_LENGTH) { plan.each[i] = PlaceStatus::TooLong; tooLong = true; }
            }
        }
    }
    // Bridges can't float: after this placement the first tile of every
    // bridge needs something on the ground pointing straight into it.
    //  * a new (or turned) bridge without that: Unconnected, nothing happens
    //  * a bridge that is supported now but wouldn't be (its belt turned or
    //    replaced, its machine gone, ...): deleted with it and refunded
    bool unconnected = false;
    if (!world.bridges().empty() || std::any_of(reqs.begin(), reqs.end(), [](const PlaceRequest& r) { return r.bridge; })) {
        std::unordered_map<Vec2i, Dir, Vec2iHash> layer, newBelts;
        std::unordered_map<Vec2i, size_t, Vec2iHash> requested;   // bridge tile -> request
        std::unordered_map<Vec2i, std::pair<MachineType, Dir>, Vec2iHash> newMachines;
        for (size_t i = 0; i < reqs.size(); ++i) {
            if (plan.each[i] != PlaceStatus::Ok) continue;
            const PlaceRequest& r = reqs[i];
            if (r.bridge) { layer[r.pos] = r.dir; requested[r.pos] = i; }
            else if (r.belt) newBelts[r.pos] = r.dir;
            else {
                Vec2i sz = machineSize(r.type, r.dir);
                for (Vec2i c : footprint(r.pos, sz.x, sz.y)) newMachines[c] = {r.type, normalizeMachineDir(r.type, r.dir)};
            }
        }
        for (auto& kv : world.bridges())
            if (!rmBelts.count(kv.second) && !layer.count(kv.first)) layer[kv.first] = world.belt(kv.second).dir;
        auto machineFeeds = [](MachineType t, Dir md, Dir travel) {
            if (t == MachineType::Splitter) return travel != opposite(md);
            if (t == MachineType::Storage) return true;   // straight away (the bridge points away)
            return isOutputSide(t, md, travel);
        };
        auto feeds = [&](Vec2i b, Dir d) {   // the ground at b after this placement
            if (auto it = newBelts.find(b); it != newBelts.end()) return it->second == d;
            if (auto it = newMachines.find(b); it != newMachines.end()) return machineFeeds(it->second.first, it->second.second, d);
            TileRef g = world.refAt(b);
            if (g > 0) return !rmBelts.count(g - 1) && world.belt(g - 1).dir == d;
            if (g < 0) {
                if (rmMachines.count(-g - 1)) return false;
                const Machine& m = world.machine(-g - 1);
                return machineFeeds(m.type, m.dir, d);
            }
            return false;
        };
        std::vector<std::pair<Vec2i, Dir>> starts;
        for (auto& kv : layer) {
            auto behind = layer.find(kv.first - dirVec(kv.second));
            if (behind == layer.end() || behind->second != kv.second) starts.push_back(kv);
        }
        for (auto& st : starts) {
            Vec2i s0 = st.first;
            Dir d = st.second;
            if (feeds(s0 - dirVec(d), d)) continue;
            std::vector<Vec2i> run;
            for (Vec2i p = s0;; p = p + dirVec(d)) {
                auto it = layer.find(p);
                if (it == layer.end() || it->second != d || run.size() > 64) break;
                run.push_back(p);
            }
            bool anyRequested = false;
            for (Vec2i p : run) anyRequested |= requested.count(p) > 0;
            if (anyRequested) {   // building a bridge that would float: refused
                for (Vec2i p : run)
                    if (auto it = requested.find(p); it != requested.end()) plan.each[it->second] = PlaceStatus::Unconnected;
                unconnected = true;
                continue;
            }
            int first = world.bridgeIdAt(s0);   // existing tiles only
            if (first >= 0 && world.bridgeSupported(first))   // it stood until now: it goes with what held it
                for (Vec2i p : run) rmBelts.insert(world.bridgeIdAt(p));
        }
    }
    plan.cost = cost;
    plan.removeBelts.assign(rmBelts.begin(), rmBelts.end());
    plan.removeMachines.assign(rmMachines.begin(), rmMachines.end());
    if (outOfReach) { plan.status = PlaceStatus::OutOfReach; return plan; }
    if (blocked) { plan.status = PlaceStatus::Blocked; return plan; }
    if (tooLong) { plan.status = PlaceStatus::TooLong; return plan; }
    if (unconnected) { plan.status = PlaceStatus::Unconnected; return plan; }
    if (undiscovered) { plan.status = PlaceStatus::Undiscovered; return plan; }
    if (onWater) { plan.status = PlaceStatus::OnWater; return plan; }
    if (needsWater) { plan.status = PlaceStatus::NeedsWater; return plan; }
    if (fluidMix) { plan.status = PlaceStatus::FluidMix; return plan; }
    if (noOre) { plan.status = PlaceStatus::NoOre; return plan; }

    // simulate on a copy: refunds in, cost out - same order as executePlacement
    for (auto& p : refundTotals(plan.removeBelts, plan.removeMachines)) addAmount(plan.delta, p.first, p.second);
    for (auto& p : refunds) addAmount(plan.delta, p.first, p.second);
    plan.refund = plan.delta;
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
    // undo: remember the old state of everything that is changed in place
    auto sameFilter = [](const ItemFilter& a, const ItemFilter& b) { return a.enabled == b.enabled && a.slots == b.slots; };
    for (size_t i = 0; i < reqs.size(); ++i) {
        int keep = plan.existing[i];
        if (keep < 0) continue;
        const PlaceRequest& r = reqs[i];
        if (r.belt) {
            const Belt& b = world.belt(keep);
            if (b.dir != r.dir || (r.setFilters && !sameFilter(b.filter, r.beltFilter))) world.noteChangedBelt(keep);
        } else {
            const Machine& m = world.machine(keep);
            bool filters = r.setFilters && !(sameFilter(m.sideFilters[0], r.sideFilters[0]) && sameFilter(m.sideFilters[1], r.sideFilters[1]) &&
                                             sameFilter(m.sideFilters[2], r.sideFilters[2]));
            bool turned = machineDirMatters(m.type) && m.dir != normalizeMachineDir(m.type, r.dir);
            if (turned || recipeChange(m, r) || filters) world.noteChangedMachine(keep);
        }
    }
    for (size_t i = 0; i < reqs.size(); ++i) {
        if (reqs[i].belt || plan.existing[i] < 0) continue;
        Machine& m = world.machine(plan.existing[i]);
        if (recipeChange(m, reqs[i]))
            for (auto& p : bufferContents(m)) player.inventory.tryAdd(p.first, p.second);
    }
    for (int b : plan.removeBelts) world.removeBelt(b);
    for (int m : plan.removeMachines) world.removeMachine(m);
    // 2. pay, then build / update - landfill first, so things can stand on it
    for (Vec2i t : plan.fill) {
        player.inventory.tryRemoveAll(landfillTileCost());
        world.addLandfill(t);
    }
    std::vector<int> ids;
    for (size_t i = 0; i < reqs.size(); ++i) {
        const PlaceRequest& r = reqs[i];
        int keep = plan.existing[i];
        if (r.belt) {
            int id = keep;
            if (id >= 0) {
                Belt& b = world.belt(id);   // kept: its item stays on it
                b.dir = r.dir;
            } else if (r.bridge) {
                player.inventory.tryRemoveAll(bridgeBuildCost());
                id = world.addBridge(r.pos, r.dir);
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
            if (machineDirMatters(m.type)) m.dir = normalizeMachineDir(m.type, r.dir);
            if (const Recipe* rc = recipeChange(m, r)) {
                m.recipe = rc;
                m.processing = false;
                m.timer = 0;
                m.resetBuffers();
            }
        } else {
            player.inventory.tryRemoveAll(machineSpec(r.type).cost);
            auto mach = makeMachine(r.type, r.pos, r.dir, r.type == MachineType::Miner ? firstOre(world, r.type, r.pos, r.dir) : NO_ITEM);
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
Vec2i Builder::machineTopLeft(MachineType t, Vec2i mouseTile, Dir d) {
    Vec2i s = machineSize(t, d);
    return {mouseTile.x - s.x / 2, mouseTile.y - s.y / 2};
}


// Shift + placing a machine on water: landfill under its footprint (the
// water tiles of it; 10 limestone each, part of the cost).
static std::vector<Vec2i> shiftFill(MachineType t, Vec2i tl, Dir dir) {
    std::vector<Vec2i> out;
    Vec2i s = machineSize(t, dir);
    for (int dy = 0; dy < s.y; ++dy)
        for (int dx = 0; dx < s.x; ++dx) out.push_back({tl.x + dx, tl.y + dy});
    return out;
}

std::vector<int> Builder::bridgesAmong(const std::vector<int>& beltIds) const {
    std::vector<int> out;
    for (int id : beltIds)
        if (world.belt(id).bridge) out.push_back(id);
    return out;
}

PlaceStatus Builder::checkMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, ItemAmounts* cost, ItemAmounts* refund,
                                  PlacePlan* planOut) const {
    PlaceRequest r;
    r.type = t;
    r.pos = machineTopLeft(t, mouseTile, dir);
    r.dir = dir;
    auto fill = allowReplace ? shiftFill(t, r.pos, dir) : std::vector<Vec2i>{};
    PlacePlan plan = planPlacement({r}, allowReplace, nullptr, false, &fill);
    if (cost) *cost = plan.cost;
    if (refund) *refund = plan.refund;
    if (planOut) *planOut = plan;
    return plan.status;
}

int Builder::placeMachine(MachineType t, Vec2i mouseTile, Dir dir, bool allowReplace, const std::set<int>* protectedIds,
                          ItemAmounts* delta) {
    PlaceRequest r;
    r.type = t;
    r.pos = machineTopLeft(t, mouseTile, dir);
    r.dir = dir;
    std::vector<PlaceRequest> reqs{r};
    auto fill = allowReplace ? shiftFill(t, r.pos, dir) : std::vector<Vec2i>{};
    PlacePlan plan = planPlacement(reqs, allowReplace, protectedIds, false, &fill);
    if (plan.status != PlaceStatus::Ok) return -1;
    if (delta) *delta = plan.delta;
    return executePlacement(reqs, plan)[0];   // new machine, or the identical one already there
}

bool Builder::canAffordMachineDeletion(int id) const {
    Inventory scratch = player.inventory;
    return applyRefunds(scratch, {}, {id});
}

// Does machine type t (facing md) push items `travel` onto the tile beside it?
static bool machinePushes(MachineType t, Dir md, Dir travel) {
    if (t == MachineType::Splitter) return travel != opposite(md);
    if (t == MachineType::Storage) return true;   // (only straight away - a bridge points straight away)
    return isOutputSide(t, md, travel);
}

std::vector<int> Builder::dependentBridges(const std::vector<int>& belts, const std::vector<int>& machines) const {
    std::unordered_set<int> gone(belts.begin(), belts.end()), out;
    std::vector<int> result;
    auto takeFrom = [&](int id) {   // this bridge tile and the rest of the bridge in front of it
        Dir d = world.belt(id).dir;
        for (Vec2i p = world.belt(id).pos;; p = p + dirVec(d)) {
            int b = world.bridgeIdAt(p);
            if (b < 0 || world.belt(b).dir != d) break;
            if (!gone.count(b) && out.insert(b).second) result.push_back(b);
        }
    };
    for (int id : belts) {
        const Belt& b = world.belt(id);
        Vec2i q = b.pos + dirVec(b.dir);
        if (b.bridge) {   // the bridge tiles in front lose their connection
            int n = world.bridgeIdAt(q);
            if (n >= 0 && world.belt(n).dir == b.dir) takeFrom(n);
        } else if (TileRef s = world.bridgeStartRef(q, b.dir)) {   // a belt feeding a bridge
            takeFrom(s - 1);
        }
    }
    for (int mid : machines) {
        const Machine& m = world.machine(mid);
        forEachOutputTile(m, [&](const OutputTile& ot) {
            if (TileRef s = world.bridgeStartRef(ot.tile, ot.dir); s && machinePushes(m.type, m.dir, ot.dir)) takeFrom(s - 1);
        });
    }
    return result;
}

bool Builder::canAffordDeletion(const std::vector<int>& belts, const std::vector<int>& machines) const {
    std::vector<int> all = belts;
    for (int b : dependentBridges(belts, machines)) all.push_back(b);
    Inventory scratch = player.inventory;
    return applyRefunds(scratch, all, machines);
}

bool Builder::deleteObjects(std::vector<int> belts, const std::vector<int>& machines, ItemAmounts* delta) {
    for (int b : dependentBridges(belts, machines)) belts.push_back(b);
    Inventory scratch = player.inventory;
    if (!applyRefunds(scratch, belts, machines)) return false;   // all or nothing
    if (delta) *delta = refundTotals(belts, machines);
    applyRefunds(player.inventory, belts, machines);
    for (int b : belts) world.removeBelt(b);
    for (int m : machines) world.removeMachine(m);
    world.markTopologyDirty();
    return true;
}

bool Builder::deleteMachineAt(Vec2i t, ItemAmounts* delta) {
    int id = world.machineIdAt(t);
    if (id < 0) return false;
    return deleteObjects({}, {id}, delta);
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

Builder::TakeResult Builder::takeContents(Machine& m, ItemAmounts* got) {
    if (got) got->clear();
    std::vector<Inventory*> sources;
    if (m.isStorage()) sources.push_back(&m.storage);
    else if (m.isProducing()) for (auto& b : m.outputs) sources.push_back(&b.inv);
    bool any = false;
    ItemAmounts moved;
    for (Inventory* src : sources)
        for (auto& p : src->contents()) {   // (a copy: src changes while moving)
            any = true;
            int n = moveAmount(*src, player.inventory, p.first, p.second);
            if (n > 0) addAmount(moved, p.first, n);
        }
    if (got) *got = moved;
    if (!any) return TakeResult::Nothing;
    return moved.empty() ? TakeResult::NoSpace : TakeResult::Ok;
}

std::unordered_set<Vec2i, Vec2iHash> Builder::supportedBridges() const {
    std::unordered_set<Vec2i, Vec2iHash> out;
    for (auto& kv : world.bridges())
        if (world.bridgeSupported(kv.second)) out.insert(kv.first);
    return out;
}

bool Builder::removeNewlyFloating(const std::unordered_set<Vec2i, Vec2iHash>& before, ItemAmounts* got, bool dryRun) {
    std::vector<int> ids;
    for (auto& kv : world.bridges())
        if (before.count(kv.first) && !world.bridgeSupported(kv.second)) ids.push_back(kv.second);
    if (got) got->clear();
    if (ids.empty()) return true;
    Inventory scratch = player.inventory;
    if (!applyRefunds(scratch, ids, {})) return false;
    if (dryRun) return true;
    if (got) *got = refundTotals(ids, {});
    applyRefunds(player.inventory, ids, {});
    for (int id : ids) world.removeBelt(id);
    world.markTopologyDirty();
    return true;
}

bool Builder::turnAt(Vec2i t, int steps, bool* noSpace, ItemAmounts* got) {
    if (noSpace) *noSpace = false;
    if (got) got->clear();
    auto before = supportedBridges();
    int id = world.beltIdAt(t), mid = world.machineIdAt(t);
    Dir old;
    if (id >= 0) old = world.belt(id).dir;
    else if (mid >= 0 && machineDirMatters(world.machine(mid).type)) old = world.machine(mid).dir;
    else return false;   // nothing there, or a miner / chest
    auto setDir = [&](Dir d) {
        if (id >= 0) world.belt(id).dir = d;   // its item stays on it
        else world.machine(mid).dir = d;
    };
    // try it: a bridge it fed must not stay in the air
    setDir(rotateCW(old, steps));
    bool ok = removeNewlyFloating(before, nullptr, true);
    setDir(old);
    if (!ok) { if (noSpace) *noSpace = true; return false; }
    if (id >= 0) world.noteChangedBelt(id);       // before changing it (undo)
    else world.noteChangedMachine(mid);
    setDir(rotateCW(old, steps));
    removeNewlyFloating(before, got);
    world.markTopologyDirty();
    return true;
}

const ItemAmounts& landfillTileCost() {
    static const ItemAmounts c = {{itemByStringId("limestone"), 10}};
    return c;
}

Builder::LandfillCheck Builder::checkLandfill(Vec2i tl, int size) const {
    LandfillCheck c;
    World& w = const_cast<World&>(world);   // reading water generates terrain
    size = std::clamp(size, LANDFILL_MIN, LANDFILL_MAX);
    for (int dy = 0; dy < size; ++dy)
        for (int dx = 0; dx < size; ++dx) {
            Vec2i t{tl.x + dx, tl.y + dy};
            if (requireExplored && !tileExplored(t)) { c.status = PlaceStatus::Undiscovered; return c; }
            if (!w.isWater(t)) continue;   // land (or already filled): left as it is
            c.tiles.push_back(t);
            for (auto& p : landfillTileCost()) addAmount(c.cost, p.first, p.second);
        }
    if (c.tiles.empty()) c.status = PlaceStatus::NoWater;
    else if (!player.inventory.hasEnough(c.cost)) c.status = PlaceStatus::NoFunds;
    return c;
}

bool Builder::placeLandfill(Vec2i tl, int size, ItemAmounts* delta) {
    LandfillCheck c = checkLandfill(tl, size);
    if (c.status != PlaceStatus::Ok) return false;
    player.inventory.tryRemoveAll(c.cost);
    for (Vec2i t : c.tiles) world.addLandfill(t);
    if (delta) { delta->clear(); for (auto& p : c.cost) addAmount(*delta, p.first, -p.second); }
    return true;
}

static ItemAmounts landfillRefund(int tiles) {
    ItemAmounts r;
    for (auto& c : landfillTileCost()) addAmount(r, c.first, c.second * tiles);
    return r;
}

bool Builder::canAffordLandfillDeletion(int tiles) const {
    Inventory scratch = player.inventory;
    for (auto& p : landfillRefund(tiles))
        if (!scratch.tryAdd(p.first, p.second)) return false;
    return true;
}

bool Builder::deleteLandfillAt(Vec2i t, ItemAmounts* delta) {
    if (!canDeleteLandfillAt(t) || !canAffordLandfillDeletion(1)) return false;
    ItemAmounts got = landfillRefund(1);
    for (auto& p : got) player.inventory.tryAdd(p.first, p.second);
    world.removeLandfill(t);
    if (delta) *delta = got;
    return true;
}

Builder::TakeResult Builder::pickUpUnderPlayer(ItemAmounts* got) {
    if (got) got->clear();
    const RectF& r = player.rect;
    int x0 = int(std::floor(r.left() / CELL_SIZE)), x1 = int(std::floor((r.right() - 1) / CELL_SIZE));
    int y0 = int(std::floor(r.top() / CELL_SIZE)), y1 = int(std::floor((r.bottom() - 1) / CELL_SIZE));
    bool any = false;
    ItemAmounts moved;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            TileRef ref = world.refAt({x, y});
            ItemId* item = nullptr;
            uint8_t* progress = nullptr;
            if (ref > 0) {
                Belt& b = world.belt(ref - 1);
                item = &b.item;
                progress = &b.progress;
            } else if (ref < 0) {
                Machine& m = world.machine(-ref - 1);
                if (!m.isSplitter()) continue;
                item = &m.item;
                progress = &m.progress;
            }
            if (!item || *item == NO_ITEM) continue;
            any = true;
            if (!player.inventory.tryAdd(*item, 1)) continue;
            addAmount(moved, *item, 1);
            *item = NO_ITEM;
            *progress = 0;
        }
    if (got) *got = moved;
    if (!any) return TakeResult::Nothing;
    return moved.empty() ? TakeResult::NoSpace : TakeResult::Ok;
}

// ---------------------------------------------------------------------------
// Bridges
// ---------------------------------------------------------------------------
std::vector<BeltPlan> Builder::planBridge(Vec2i start, Vec2i end, Dir dir) {
    Vec2i d = end - start;
    int n = 1;
    if (d.x != 0 || d.y != 0) {
        if (std::abs(d.x) >= std::abs(d.y)) { dir = d.x > 0 ? Dir::Right : Dir::Left; n = std::abs(d.x) + 1; }
        else { dir = d.y > 0 ? Dir::Down : Dir::Up; n = std::abs(d.y) + 1; }
    }
    n = std::min(n, BRIDGE_MAX_LENGTH);
    std::vector<BeltPlan> plan;
    for (int i = 0; i < n; ++i) plan.push_back({start + dirVec(dir) * i, dir, std::nullopt});
    return plan;
}

static std::vector<PlaceRequest> bridgeRequests(const std::vector<BeltPlan>& plan) {
    std::vector<PlaceRequest> reqs;
    for (auto& p : plan) {
        PlaceRequest r;
        r.belt = r.bridge = true;
        r.pos = p.pos;
        r.dir = p.dir;
        reqs.push_back(r);
    }
    return reqs;
}

PlacePlan Builder::checkBridge(const std::vector<BeltPlan>& plan, bool allowReplace) const {
    return planPlacement(bridgeRequests(plan), allowReplace);
}

bool Builder::placeBridge(const std::vector<BeltPlan>& plan, bool allowReplace, ItemAmounts* delta) {
    if (plan.empty()) return false;
    auto reqs = bridgeRequests(plan);
    PlacePlan pp = planPlacement(reqs, allowReplace);
    if (pp.status != PlaceStatus::Ok) return false;
    if (delta) *delta = pp.delta;
    executePlacement(reqs, pp);
    return true;
}

std::vector<int> Builder::bridgeRun(int id) const {
    const Belt& b = world.belt(id);
    std::vector<int> run{id};
    for (int s = -1; s <= 1; s += 2)
        for (Vec2i p = b.pos + dirVec(b.dir) * s;; p = p + dirVec(b.dir) * s) {
            int o = world.bridgeIdAt(p);
            if (o < 0 || world.belt(o).dir != b.dir) break;
            run.push_back(o);
        }
    return run;
}

bool Builder::deleteBridgeAt(Vec2i t, bool whole, ItemAmounts* delta) {
    int id = world.bridgeIdAt(t);
    if (id < 0) return false;
    std::vector<int> ids = whole ? bridgeRun(id) : std::vector<int>{id};
    return deleteObjects(ids, {}, delta);   // (with the rest of the bridge in front of it)
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
    // water: only a belt that is already there may stay (turned in place)
    if (beltTileOnWater(p.pos)) return true;
    if (allowReplace) return false;
    if (!world.isCellBlocked(p.pos)) return false;
    if (p.pos == start) return !(world.beltIdAt(p.pos) >= 0 && othersOk);
    return !(segmentMatchesExisting(p) || segmentReversesExisting(p));
}

bool Builder::anyBeltTileOnWater(const std::vector<BeltPlan>& plan) const {
    for (auto& p : plan)
        if (beltTileOnWater(p.pos)) return true;
    return false;
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

PlaceStatus Builder::beltAffordability(const std::vector<BeltPlan>& plan, ItemAmounts* cost, ItemAmounts* refund,
                                       PlacePlan* planOut) const {
    // blocking is checked separately (drag rules); here only items matter
    PlacePlan pp = planPlacement(beltRequests(plan), true);
    if (cost) *cost = pp.cost;
    if (refund) *refund = pp.refund;
    if (planOut) *planOut = pp;
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
    return deleteObjects(ids, {}, delta);   // (with the bridges it fed)
}

// ---------------------------------------------------------------------------
// Blueprints
// ---------------------------------------------------------------------------
static std::vector<PlaceRequest> blueprintRequests(const Blueprint& bp, Vec2i tl) {
    std::vector<PlaceRequest> reqs;
    for (auto& b : bp.belts) {
        PlaceRequest r;
        r.belt = true;
        r.bridge = b.bridge;
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

static std::vector<Vec2i> blueprintFill(const Blueprint& bp, Vec2i tl) {
    std::vector<Vec2i> out;
    out.reserve(bp.landfill.size());
    for (Vec2i o : bp.landfill) out.push_back(tl + o);
    return out;
}

Builder::BlueprintCheck Builder::checkBlueprint(const Blueprint& bp, Vec2i tl, bool allowReplace) const {
    auto fill = blueprintFill(bp, tl);
    PlacePlan plan = planPlacement(blueprintRequests(bp, tl), allowReplace, nullptr, true, &fill);
    BlueprintCheck c;
    c.status = plan.status;
    c.cost = plan.cost;
    c.refund = plan.refund;
    c.removeBelts = plan.removeBelts;
    c.removeMachines = plan.removeMachines;
    c.belts.assign(plan.each.begin(), plan.each.begin() + long(bp.belts.size()));
    c.machines.assign(plan.each.begin() + long(bp.belts.size()), plan.each.end());
    return c;
}

bool Builder::placeBlueprint(const Blueprint& bp, Vec2i tl, bool allowReplace, ItemAmounts* delta) {
    if (bp.empty()) return false;
    auto reqs = blueprintRequests(bp, tl);
    auto fill = blueprintFill(bp, tl);
    PlacePlan plan = planPlacement(reqs, allowReplace, nullptr, true, &fill);
    if (plan.status != PlaceStatus::Ok) return false;   // all or nothing
    if (delta) *delta = plan.delta;
    executePlacement(reqs, plan);
    return true;
}

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------
// What one belt / machine is worth: build cost + what is inside.
static ItemAmounts snapValue(const ObjSnap& s) {
    if (!s.belt) return s.machine->refundItems();
    ItemAmounts v = beltCost(s.bridge);
    if (s.beltState.item != NO_ITEM) addAmount(v, s.beltState.item, 1);
    return v;
}

Builder::UndoResult Builder::undo(const UndoRecord& rec, ItemAmounts* delta, UndoRecord* inverse) {
    if (rec.empty()) return UndoResult::Nothing;
    // 1. everything the action added must still be there
    std::vector<int> belts, machines;
    std::unordered_set<int> beltSet, machineSet;
    for (const ObjSnap& a : rec.added) {
        if (a.belt) {
            int id = a.bridge ? world.bridgeIdAt(a.pos) : world.beltIdAt(a.pos);
            if (id < 0) return UndoResult::Changed;
            if (beltSet.insert(id).second) belts.push_back(id);
        } else {
            int id = world.machineIdAt(a.pos);
            if (id < 0 || world.machine(id).pos != a.pos || world.machine(id).type != a.type) return UndoResult::Changed;
            if (machineSet.insert(id).second) machines.push_back(id);
        }
    }
    // 2. the old things must fit where they were (what step 1 removes is free)
    std::unordered_set<Vec2i, Vec2iHash> freed, freedBridges;   // ground / bridge layer
    for (int id : belts) (world.belt(id).bridge ? freedBridges : freed).insert(world.belt(id).pos);
    for (int id : machines) {
        const Machine& m = world.machine(id);
        for (Vec2i c : footprint(m.pos, m.w, m.h)) freed.insert(c);
    }
    // landfill: tiles to turn back into water must still be landfill with
    // nothing on them (except what this undo removes); tiles to fill again
    // must be water
    for (Vec2i t : rec.landfillAdded) {
        if (!world.isLandfill(t)) return UndoResult::Changed;
        if (world.isCellBlocked(t) && !freed.count(t)) return UndoResult::Blocked;
    }
    for (Vec2i t : rec.landfillRemoved)
        if (!world.isWater(t)) return UndoResult::Changed;
    std::unordered_set<Vec2i, Vec2iHash> taken, takenBridges, takenMachines;
    for (const ObjSnap& s : rec.removed) {
        if (s.belt && s.bridge) {   // bridge layer: free there, and no machine under it
            Vec2i c = s.pos;
            if ((world.bridgeIdAt(c) >= 0 && !freedBridges.count(c)) || !takenBridges.insert(c).second || takenMachines.count(c))
                return UndoResult::Blocked;
            if (world.machineIdAt(c) >= 0 && !freed.count(c)) return UndoResult::Blocked;
            continue;
        }
        int w = s.belt ? 1 : s.machine->w, h = s.belt ? 1 : s.machine->h;
        for (Vec2i c : footprint(s.pos, w, h)) {
            if ((world.isCellBlocked(c) && !freed.count(c)) || !taken.insert(c).second) return UndoResult::Blocked;
            if (isBlockedByPlayer(c)) return UndoResult::PlayerInTheWay;
            if (!s.belt) {   // a machine can't go under a bridge
                if ((world.bridgeIdAt(c) >= 0 && !freedBridges.count(c)) || takenBridges.count(c)) return UndoResult::Blocked;
                takenMachines.insert(c);
            }
        }
    }
    // 3. items: get back what the added things are worth now, pay what the
    //    old things were worth (build cost + their contents) - all or nothing
    ItemAmounts net;
    for (auto& p : refundTotals(belts, machines)) addAmount(net, p.first, p.second);
    for (const ObjSnap& s : rec.removed)
        for (auto& p : snapValue(s)) addAmount(net, p.first, -p.second);
    for (auto& c : landfillTileCost()) {
        addAmount(net, c.first, c.second * int(rec.landfillAdded.size()));     // limestone back
        addAmount(net, c.first, -c.second * int(rec.landfillRemoved.size()));  // filled again: pay
    }
    ItemAmounts pay, get;
    for (auto& p : net) {
        if (p.second < 0) addAmount(pay, p.first, -p.second);
        else if (p.second > 0) addAmount(get, p.first, p.second);
    }
    Inventory scratch = player.inventory;
    if (!scratch.tryRemoveAll(pay)) return UndoResult::NoFunds;
    for (auto& p : get)
        if (!scratch.tryAdd(p.first, p.second)) return UndoResult::NoSpace;
    // 4. do it, recording what this does: that is exactly how to redo it
    auto supportedBefore = supportedBridges();
    std::unordered_set<Vec2i, Vec2iHash> hadBridge;
    for (auto& kv : world.bridges()) hadBridge.insert(kv.first);
    bool rec0 = world.recordChanges;
    world.takeRecord();   // (the caller hands over anything pending first)
    world.recordChanges = true;
    player.inventory = scratch;
    for (int id : belts) world.removeBelt(id);
    for (int id : machines) world.removeMachine(id);
    for (Vec2i t : rec.landfillAdded) world.removeLandfill(t);
    for (Vec2i t : rec.landfillRemoved) world.addLandfill(t);
    for (const ObjSnap& s : rec.removed) {
        if (s.belt) {
            const Belt& old = s.beltState;
            int id = old.bridge ? world.addBridge(old.pos, old.dir) : world.addBelt(old.pos, old.dir);
            Belt& b = world.belt(id);
            b.filter = old.filter;
            b.item = old.item;
            b.progress = old.progress;
            b.itemFrom = old.itemFrom;
        } else {
            auto m = std::make_unique<Machine>(*s.machine);
            m->anims.clear();
            m->reservedFor = -1;
            m->outBelts.clear();
            m->outDirs.clear();
            world.addMachine(std::move(m));
        }
    }
    // 5. bridges can't float: one it brought back without support -> take it
    //    all back; ones it leaves without support go too (refunded)
    UndoResult fail = UndoResult::Ok;
    for (auto& kv : world.bridges())
        if (!hadBridge.count(kv.first) && !world.bridgeSupported(kv.second)) fail = UndoResult::Floating;
    ItemAmounts extra;
    if (fail == UndoResult::Ok && !removeNewlyFloating(supportedBefore, &extra)) fail = UndoResult::NoSpace;
    if (fail != UndoResult::Ok) {
        UndoRecord partial = world.takeRecord();
        world.recordChanges = rec0;
        undo(partial);   // exactly back to how it was
        world.markTopologyDirty();
        world.ensureTopology();
        return fail;
    }
    for (auto& p : extra) addAmount(net, p.first, p.second);
    UndoRecord inv = world.takeRecord();
    if (inverse) *inverse = std::move(inv);
    world.recordChanges = rec0;
    world.markTopologyDirty();
    world.ensureTopology();
    if (delta) *delta = net;
    return UndoResult::Ok;
}

// ---------------------------------------------------------------------------
// Area delete
// ---------------------------------------------------------------------------
Builder::AreaDelete Builder::deleteArea(Vec2i a, Vec2i b, ItemAmounts* refunded, int* removed, Layers layers) {
    // same rules as Copy area: things (belts, bridges, machines) if there are any, otherwise landfill
    SelectionContent sc = gatherContent(world, makeSelection(a, b), SelectRule::ObjectsFirst, layers);
    if (sc.empty()) return AreaDelete::Nothing;

    if (!sc.landfill.empty()) {   // (then there are no belts / machines)
        int n = int(sc.landfill.size());
        if (!canAffordLandfillDeletion(n)) return AreaDelete::NoSpace;
        ItemAmounts got = landfillRefund(n);
        for (auto& p : got) player.inventory.tryAdd(p.first, p.second);
        for (Vec2i t : sc.landfill) world.removeLandfill(t);
        if (refunded) *refunded = got;
        if (removed) *removed = n;
        return AreaDelete::Ok;
    }

    std::vector<int> belts = sc.belts;   // (bridges are belts too)
    belts.insert(belts.end(), sc.bridges.begin(), sc.bridges.end());
    for (int b : dependentBridges(belts, sc.machines)) belts.push_back(b);   // bridges that would float
    const std::vector<int>& machines = sc.machines;
    Inventory scratch = player.inventory;
    if (!applyRefunds(scratch, belts, machines)) return AreaDelete::NoSpace;

    if (refunded) *refunded = refundTotals(belts, machines);
    if (removed) *removed = int(belts.size() + machines.size());
    applyRefunds(player.inventory, belts, machines);
    for (int id : belts) world.removeBelt(id);
    for (int id : machines) world.removeMachine(id);
    return AreaDelete::Ok;
}
