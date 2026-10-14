// tests/sim_tests.cpp - headless tests for the simulation.
//   * belts are exactly 4 items/s, regardless of length (no lost ticks)
//   * merges, splitters and loops don't lose throughput
//   * building / deleting / crafting / transport never create or destroy items
//   * terrain matches the Python generator, saves round-trip
#include "../src/sim/World.hpp"
#include "../src/sim/Build.hpp"
#include "../src/sim/Save.hpp"
#include "../src/sim/Terrain.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

static int g_failed = 0, g_passed = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_passed; } else { ++g_failed; std::printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static ItemId IRON_ORE, IRON_INGOT, COPPER_INGOT, COPPER_ORE, COAL;

// A storage box full of `item` with a belt line leaving it.
static int makeSourceStorage(World& w, Vec2i pos, ItemId item, int amount) {
    auto m = makeMachine(MachineType::Storage, pos);
    while (amount > 0) { int n = std::min(amount, 100); m->storage.tryAdd(item, n); amount -= n; }
    return w.addMachine(std::move(m));
}
static int makeSinkStorage(World& w, Vec2i pos) { return w.addMachine(makeMachine(MachineType::Storage, pos)); }

static void line(World& w, Vec2i start, Dir d, int len) {
    for (int i = 0; i < len; ++i) w.addBelt(start + dirVec(d) * i, d);
}

static int total(const ItemAmounts& a) { int t = 0; for (auto& p : a) t += p.second; return t; }

static std::map<ItemId, int> asMap(const ItemAmounts& a) {
    std::map<ItemId, int> m;
    for (auto& p : a) m[p.first] += p.second;
    return m;
}

// ---------------------------------------------------------------------------
static void testStraightBeltThroughput(int length) {
    World w;
    w.seed = 1;
    // Source storage occupies x=-2..-1, y=0..1; line starts at x=0,y=0 going right.
    makeSourceStorage(w, {-2, 0}, IRON_INGOT, 4000);
    line(w, {0, 0}, Dir::Right, length);
    int sink = makeSinkStorage(w, {length, 0});

    // First item must arrive after exactly length * 15 ticks (+1 for the
    // tick it is pushed on, +... see below) and then exactly every 15 ticks.
    std::vector<uint64_t> arrivals;
    int last = 0;
    for (int t = 0; t < length * 15 + 60 * 60; ++t) {
        w.tick();
        int now = w.machine(sink).storage.amount(IRON_INGOT);
        for (int k = last; k < now; ++k) arrivals.push_back(w.tickCount());
        last = now;
    }
    CHECK(!arrivals.empty(), "no items arrived (length %d)", length);
    if (arrivals.empty()) return;
    // The first item enters tile 0 on tick 1 and needs 15 ticks per tile.
    CHECK(arrivals[0] == uint64_t(length) * 15 + 1, "first arrival at tick %llu, expected %d (length %d)",
          (unsigned long long)arrivals[0], length * 15 + 1, length);
    bool steady = true;
    for (size_t i = 1; i < arrivals.size(); ++i)
        if (arrivals[i] - arrivals[i - 1] != 15) { steady = false; break; }
    CHECK(steady, "gaps between arrivals are not exactly 15 ticks (length %d)", length);
    // Items per second at the end of the belt, counted over 60 s.
    int perMinute = 0;
    uint64_t from = arrivals[0];
    for (auto a : arrivals) if (a >= from && a < from + 3600) ++perMinute;
    CHECK(perMinute == 240, "throughput %d items/min, expected 240 (length %d)", perMinute, length);
    std::printf("  straight belt %5d tiles: first item after %llu ticks, %d items/min\n", length,
                (unsigned long long)arrivals[0], perMinute);
}

// ---------------------------------------------------------------------------
static void testMerge() {
    World w;
    w.seed = 1;
    // Two sources feeding one line at a T-junction.
    //   A: line from the left into (20,0); B: line from below into (20,0).
    makeSourceStorage(w, {-2, 0}, IRON_INGOT, 4000);
    line(w, {0, 0}, Dir::Right, 20);               // 0..19 -> into (20,0)
    makeSourceStorage(w, {20, 22}, COPPER_INGOT, 4000);  // occupies 20..21, 22..23
    line(w, {20, 21}, Dir::Up, 21);                // (20,21) .. (20,1) going up into (20,0)
    line(w, {20, 0}, Dir::Right, 30);              // the merged line (20..49)
    int sink = makeSinkStorage(w, {50, 0});
    for (int t = 0; t < 2000; ++t) w.tick();
    int a0 = w.machine(sink).storage.amount(IRON_INGOT), b0 = w.machine(sink).storage.amount(COPPER_INGOT);
    for (int t = 0; t < 3600; ++t) w.tick();
    int a = w.machine(sink).storage.amount(IRON_INGOT) - a0, b = w.machine(sink).storage.amount(COPPER_INGOT) - b0;
    CHECK(a + b == 240, "merged output %d/min, expected 240", a + b);
    CHECK(a == 120 && b == 120, "merge not fair: %d iron vs %d copper", a, b);
    std::printf("  merge: %d + %d = %d items/min\n", a, b, a + b);
}

// ---------------------------------------------------------------------------
static void testSplitter() {
    World w;
    w.seed = 1;
    makeSourceStorage(w, {-2, 0}, IRON_INGOT, 6000);
    line(w, {0, 0}, Dir::Right, 10);
    w.addMachine(makeMachine(MachineType::Splitter, {10, 0}, Dir::Right));
    // three outputs: up (left side), right (forward), down (right side)
    line(w, {10, -1}, Dir::Up, 10);  int sUp = makeSinkStorage(w, {10, -12});
    line(w, {11, 0}, Dir::Right, 10); int sFwd = makeSinkStorage(w, {21, 0});
    line(w, {10, 1}, Dir::Down, 10); int sDown = makeSinkStorage(w, {10, 11});
    for (int t = 0; t < 1000; ++t) w.tick();
    auto count = [&]() {
        return std::array<int, 3>{w.machineAlive(sUp) ? w.machine(sUp).storage.amount(IRON_INGOT) : 0, w.machine(sFwd).storage.amount(IRON_INGOT),
                                  w.machine(sDown).storage.amount(IRON_INGOT)};
    };
    auto c0 = count();
    for (int t = 0; t < 3600; ++t) w.tick();
    auto c1 = count();
    int u = c1[0] - c0[0], f = c1[1] - c0[1], d = c1[2] - c0[2];
    CHECK(u + f + d == 240, "splitter total %d/min, expected 240", u + f + d);
    CHECK(u == 80 && f == 80 && d == 80, "splitter uneven %d/%d/%d", u, f, d);
    std::printf("  splitter: %d / %d / %d = %d items/min\n", u, f, d, u + f + d);

    // Block one output: the other two must take the full 4 items/s.
    w.removeMachine(w.machineIdAt({10, -12}));
    for (int t = 0; t < 1000; ++t) w.tick();
    c0 = count();
    for (int t = 0; t < 3600; ++t) w.tick();
    c1 = count();
    f = c1[1] - c0[1]; d = c1[2] - c0[2];
    CHECK(f + d == 240, "splitter with one blocked side %d/min, expected 240", f + d);
    std::printf("  splitter (one side blocked): %d + %d = %d items/min\n", f, d, f + d);
}

// A splitter with a belt on one side and machines on the others must feed
// all of them (the belt used to win every time and the machine got nothing).
// (Smelters on both sides: a chest touching the belt line would push its ore
// straight back onto it.)
static void testSplitterIntoMachine() {
    World w;
    w.seed = 1;
    makeSourceStorage(w, {-2, 0}, IRON_ORE, 600);
    line(w, {0, 0}, Dir::Right, 5);
    w.addMachine(makeMachine(MachineType::Splitter, {5, 0}, Dir::Right));
    line(w, {6, 0}, Dir::Right, 6);
    int sink = makeSinkStorage(w, {12, 0});
    int top = w.addMachine(makeMachine(MachineType::Smelter, {4, -3}));     // left side (up)
    int bottom = w.addMachine(makeMachine(MachineType::Smelter, {4, 1}));   // right side (down)
    auto held = [&](int id) { int n = 0; for (auto& c : w.machine(id).contents()) n += c.second; return n; };
    for (int t = 0; t < 600; ++t) w.tick();
    int toTop = held(top), toBottom = held(bottom);
    int toBelt = w.machine(sink).storage.amount(IRON_ORE);
    CHECK(toTop > 0 && toBottom > 0 && toBelt > 0, "splitter outputs: belt %d, top smelter %d, bottom smelter %d", toBelt, toTop, toBottom);
    std::printf("  splitter into machines: belt %d, smelters %d / %d\n", toBelt, toTop, toBottom);
}

// ---------------------------------------------------------------------------
static void testFullLoopRotates() {
    World w;
    w.seed = 1;
    // 4x3 ring, every tile filled.
    std::vector<int> ids;
    for (int x = 0; x < 4; ++x) ids.push_back(w.addBelt({x, 0}, Dir::Right));
    for (int y = 0; y < 2; ++y) ids.push_back(w.addBelt({4, y}, Dir::Down));
    for (int x = 4; x > 0; --x) ids.push_back(w.addBelt({x, 2}, Dir::Left));
    for (int y = 2; y > 0; --y) ids.push_back(w.addBelt({0, y}, Dir::Up));
    // fix corner (0,0) & (4,2): rebuild cleanly
    w.clear();
    w.seed = 1;
    ids.clear();
    // ring tiles in travel order
    std::vector<std::pair<Vec2i, Dir>> ring;
    for (int x = 0; x < 4; ++x) ring.push_back({{x, 0}, Dir::Right});
    for (int y = 0; y < 2; ++y) ring.push_back({{4, y}, Dir::Down});
    for (int x = 4; x > 0; --x) ring.push_back({{x, 2}, Dir::Left});
    for (int y = 2; y > 0; --y) ring.push_back({{0, y}, Dir::Up});
    for (auto& r : ring) ids.push_back(w.addBelt(r.first, r.second));
    for (size_t i = 0; i < ids.size(); ++i) { w.belt(ids[i]).item = i % 2 ? IRON_INGOT : COPPER_INGOT; w.belt(ids[i]).progress = 15; }
    ItemId first = w.belt(ids[0]).item;
    int n = int(ids.size());
    for (int t = 0; t < 15 * n; ++t) w.tick();
    // after n tiles of travel every item is back where it started
    CHECK(w.belt(ids[0]).item == first, "full loop did not rotate");
    CHECK(total(w.countItems()) == n, "loop lost items: %d of %d", total(w.countItems()), n);
    // and after half a tile's worth of ticks it must have moved one tile
    for (int t = 0; t < 15; ++t) w.tick();
    CHECK(w.belt(ids[1]).item == first, "full loop is not moving at full speed");
}

// ---------------------------------------------------------------------------
static void testProductionChain() {
    World w;
    w.seed = 1;
    // storage(ore) -> belt -> smelter -> belt -> storage
    makeSourceStorage(w, {-2, 0}, IRON_ORE, 900);
    line(w, {0, 0}, Dir::Right, 5);
    w.addMachine(makeMachine(MachineType::Smelter, {5, -1}));   // 5..7, -1..1
    line(w, {8, 0}, Dir::Right, 5);
    int sink = makeSinkStorage(w, {13, 0});
    for (int t = 0; t < 60 * 60; ++t) w.tick();
    int ingots = w.machine(sink).storage.amount(IRON_INGOT);
    // 1 ingot per 2 s -> ~30 per minute (minus start-up time)
    CHECK(ingots >= 27 && ingots <= 30, "smelter produced %d ingots/min, expected ~30", ingots);
    // Conservation through recipes: ore = 3 * ingots
    auto m = asMap(w.countItems());
    CHECK(m[IRON_ORE] + 3 * m[IRON_INGOT] == 900, "ore/ingot balance broken: %d ore + 3*%d ingots", m[IRON_ORE], m[IRON_INGOT]);
    std::printf("  smelter chain: %d ingots in 60 s\n", ingots);
}

// ---------------------------------------------------------------------------
// Side outputs: machines and chests push onto every belt running past their
// sides (joined like an intersection) - input lines go one tile away and end
// pointing into the machine.
//
//   y=-1   >>>>>  (to the sink chest at x 4..5)    smelter x 0..2, y 0..2
//   y= 1   >> into the smelter's left side, from a chest at x -4..-3
static void testSideOutputs() {
    World w;
    w.seed = 1;
    makeSourceStorage(w, {-4, 0}, IRON_ORE, 300);                     // -4..-3, 0..1
    w.addBelt({-2, 1}, Dir::Right);
    w.addBelt({-1, 1}, Dir::Right);                                    // into the smelter
    int sm = w.addMachine(makeMachine(MachineType::Smelter, {0, 0}));  // 0..2, 0..2
    line(w, {0, -1}, Dir::Right, 4);                                   // 0..3 past its top
    int sink = makeSinkStorage(w, {4, -2});                            // 4..5, -2..-1
    w.ensureTopology();
    CHECK(w.beltAt({1, -1})->incomingMask == (dirBit(Dir::Right) | dirBit(Dir::Up)), "belt past the top not joined (mask %d)",
          w.beltAt({1, -1})->incomingMask);
    CHECK(w.beltAt({0, -1})->incomingMask == dirBit(Dir::Up), "first belt past the top: mask %d", w.beltAt({0, -1})->incomingMask);
    CHECK(w.machine(sm).outBelts.size() == 3, "smelter has %d output belts, expected 3", int(w.machine(sm).outBelts.size()));
    CHECK(w.beltAt({-1, 1})->incomingMask == dirBit(Dir::Right), "the belt into the smelter got joined");

    bool ingotOnInput = false;
    for (int t = 0; t < 60 * 60; ++t) {
        w.tick();
        for (int x = -2; x <= -1; ++x)
            if (w.beltAt({x, 1})->item == IRON_INGOT) ingotOnInput = true;
    }
    CHECK(!ingotOnInput, "an ingot ended up on the input belts");
    int got = w.machine(sink).storage.amount(IRON_INGOT);
    CHECK(got >= 27, "side output delivered %d ingots in 60 s", got);
    auto m = asMap(w.countItems());
    CHECK(m[IRON_ORE] + 3 * m[IRON_INGOT] == 300, "ore/ingot balance broken: %d ore + 3*%d ingots", m[IRON_ORE], m[IRON_INGOT]);
    std::printf("  side outputs: %d ingots in 60 s\n", got);

    // No exceptions: a belt running past a machine is always an output, even
    // if the same line turns into the machine later.
    World v;
    v.seed = 1;
    int s2 = v.addMachine(makeMachine(MachineType::Smelter, {20, 0}));  // 20..22, 0..2
    line(v, {19, -1}, Dir::Right, 4);                                  // 19..22 above it
    v.addBelt({23, -1}, Dir::Down);
    v.addBelt({23, 0}, Dir::Left);                                     // into the smelter
    v.ensureTopology();
    CHECK(v.machine(s2).outBelts.size() == 3, "%d side outputs, expected 3", int(v.machine(s2).outBelts.size()));
    // previews (overlay) follow the same rule
    World::Overlay ov;
    ov.belts[{21, 3}] = Dir::Left;
    CHECK(v.computeIncoming({21, 3}, Dir::Left, &ov) == dirBit(Dir::Down), "preview belt below the smelter not joined");

    // storage chests work the same way
    World u;
    u.seed = 1;
    makeSourceStorage(u, {0, 0}, IRON_INGOT, 10);                      // 0..1, 0..1
    line(u, {-1, -1}, Dir::Right, 4);                                  // past its top side ...
    int sinkC = makeSinkStorage(u, {3, -2});                           // ... into another chest
    u.ensureTopology();
    CHECK(u.beltAt({0, -1})->incomingMask == (dirBit(Dir::Right) | dirBit(Dir::Up)), "chest not joined to a passing belt (mask %d)",
          u.beltAt({0, -1})->incomingMask);
    for (int t = 0; t < 600; ++t) u.tick();
    CHECK(u.machine(sinkC).storage.amount(IRON_INGOT) == 10, "chest side output delivered %d of 10", u.machine(sinkC).storage.amount(IRON_INGOT));
}

// Ctrl + left click: take a machine's outputs / a chest's contents.
static void testTakeContents() {
    World w;
    w.seed = 1;
    Player p;
    Builder b(w, p);
    // smelter: outputs are taken, inputs stay
    int sid = w.addMachine(makeMachine(MachineType::Smelter, {0, 0}));
    Machine& sm = w.machine(sid);
    sm.inputs[0].inv.tryAdd(sm.inputs[0].item, 5);
    sm.outputs[0].inv.tryAdd(sm.outputs[0].item, 7);
    ItemAmounts got;
    CHECK(b.takeContents(sm, &got) == Builder::TakeResult::Ok, "smelter take failed");
    CHECK(total(got) == 7 && p.inventory.amount(sm.outputs[0].item) == 7, "took %d outputs, expected 7", total(got));
    CHECK(sm.inputs[0].inv.amount(sm.inputs[0].item) == 5, "inputs were taken too");
    CHECK(b.takeContents(sm, &got) == Builder::TakeResult::Nothing && got.empty(), "empty outputs not reported");
    // chest: everything, as far as it fits
    int cid = makeSourceStorage(w, {10, 0}, IRON_INGOT, 250);
    w.machine(cid).storage.tryAdd(COAL, 30);
    CHECK(b.takeContents(w.machine(cid), &got) == Builder::TakeResult::Ok, "chest take failed");
    auto g = asMap(got);
    CHECK(g[IRON_INGOT] == 250 && g[COAL] == 30 && w.machine(cid).storage.isEmpty(), "chest gave %d iron, %d coal", g[IRON_INGOT], g[COAL]);
    // full inventory: what fits moves, the rest stays; nothing moved = NoSpace
    p.inventory.clear();
    int cap = 0;
    while (p.inventory.tryAdd(COPPER_INGOT, 100)) cap += 100;
    p.inventory.tryRemove(COPPER_INGOT, 40);
    makeSourceStorage(w, {20, 0}, COPPER_INGOT, 100);
    int c2 = w.machineIdAt({20, 0});
    CHECK(b.takeContents(w.machine(c2), &got) == Builder::TakeResult::Ok && total(got) == 40, "partial take moved %d, expected 40", total(got));
    CHECK(w.machine(c2).storage.amount(COPPER_INGOT) == 60, "chest kept %d, expected 60", w.machine(c2).storage.amount(COPPER_INGOT));
    CHECK(b.takeContents(w.machine(c2), &got) == Builder::TakeResult::NoSpace, "full inventory not reported");
    CHECK(p.inventory.amount(COPPER_INGOT) + w.machine(c2).storage.amount(COPPER_INGOT) == cap + 60, "copper lost or created");
    std::printf("  take contents: outputs only from machines, everything from chests, as much as fits\n");
}

// E: pick up items from the belts / splitters under the player.
static void testPickUpUnderPlayer() {
    World w;
    w.seed = 1;
    Player p;
    Builder b(w, p);
    int b1 = w.addBelt({0, 0}, Dir::Right), b2 = w.addBelt({1, 0}, Dir::Right), far = w.addBelt({5, 0}, Dir::Right);
    int sp = w.addMachine(makeMachine(MachineType::Splitter, {0, 1}, Dir::Right));
    w.belt(b1).item = IRON_ORE;
    w.belt(b2).item = COAL;
    w.belt(far).item = COAL;
    w.machine(sp).item = IRON_ORE;
    p.setCenter(CELL_SIZE, CELL_SIZE);   // straddles tiles (0,0) (1,0) (0,1) (1,1)
    ItemAmounts got;
    CHECK(b.pickUpUnderPlayer(&got) == Builder::TakeResult::Ok, "pick up failed");
    auto g = asMap(got);
    CHECK(g[IRON_ORE] == 2 && g[COAL] == 1, "picked up %d ore, %d coal", g[IRON_ORE], g[COAL]);
    CHECK(w.belt(b1).item == NO_ITEM && w.machine(sp).item == NO_ITEM && w.belt(far).item == COAL, "wrong tiles emptied");
    CHECK(b.pickUpUnderPlayer(&got) == Builder::TakeResult::Nothing, "empty tiles not reported");
    // full inventory: the item stays on the belt
    while (p.inventory.tryAdd(COPPER_INGOT, 1)) {}
    while (p.inventory.tryAdd(COAL, 1)) {}   // (the coal stack from above has room too)
    w.belt(b2).item = COAL;
    CHECK(b.pickUpUnderPlayer(&got) == Builder::TakeResult::NoSpace && w.belt(b2).item == COAL, "full inventory lost the item");
    std::printf("  pick up (E): belts and splitters under the player, as much as fits\n");
}

// ---------------------------------------------------------------------------
// Value of everything = items held + build cost of everything built.
static std::map<ItemId, int> worldValue(World& w, Player& p) {
    std::map<ItemId, int> v = asMap(w.countItems());
    for (auto& s : p.inventory.contents()) v[s.first] += s.second;
    for (auto& b : w.belts()) if (b.alive) for (auto& c : beltBuildCost()) v[c.first] += c.second;
    for (size_t i = 0; i < w.machineSlots(); ++i)
        if (w.machineAlive(int(i))) for (auto& c : machineSpec(w.machine(int(i)).type).cost) v[c.first] += c.second;
    return v;
}

static void testConservationFuzz() {
    World w;
    w.seed = 12345;
    Player p;
    p.inventory.tryAdd(IRON_INGOT, 2000);
    p.inventory.tryAdd(COPPER_INGOT, 900);
    p.setCenter(-5000, -5000);   // keep the player out of the way
    Builder b(w, p);
    // some items flowing around
    makeSourceStorage(w, {-2, 0}, COPPER_ORE, 300);
    auto start = worldValue(w, p);

    std::mt19937 rng(7);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int built = 0, deleted = 0, undone = 0;
    w.recordChanges = true;          // and undo things at random
    std::vector<UndoRecord> undoStack;
    for (int step = 0; step < 6000; ++step) {
        int op = rnd(0, 9);
        Vec2i t{rnd(-6, 14), rnd(-6, 8)};
        bool shift = rnd(0, 3) == 0;
        if (op <= 2) {
            Vec2i e{t.x + rnd(-6, 6), t.y + rnd(-6, 6)};
            if (b.placeBelts(t, e, Dir(rnd(0, 3)), shift)) ++built;
        } else if (op <= 4) {
            MachineType types[] = {MachineType::Smelter, MachineType::Assembler, MachineType::Splitter, MachineType::Storage};
            if (b.placeMachine(types[rnd(0, 3)], t, Dir(rnd(0, 3)), shift, nullptr) >= 0) ++built;
        } else if (op <= 6) {
            if (b.deleteMachineAt(t) || b.deleteBeltAt(t, shift)) ++deleted;
        } else if (op == 7) {
            // switch a recipe somewhere
            Machine* m = w.machineAt(t);
            if (m && m->recipeChoices()) b.setRecipe(*m, (*m->recipeChoices())[size_t(rnd(0, int(m->recipeChoices()->size()) - 1))]);
        } else if (op == 8 && rnd(0, 1) == 0) {
            // copy a random area and paste it somewhere (sometimes with Shift = replace)
            Blueprint bp = captureBlueprint(w, t, {t.x + rnd(-5, 5), t.y + rnd(-5, 5)});
            if (!bp.empty() && b.placeBlueprint(bp, {rnd(-6, 14), rnd(-6, 8)}, shift)) ++built;
        } else if (op == 9 && rnd(0, 2) == 0 && !undoStack.empty()) {
            if (b.undo(undoStack.back()) == Builder::UndoResult::Ok) ++undone;
            undoStack.pop_back();
        } else {
            for (int k = 0; k < 40; ++k) w.tick();
        }
        // No recipes can run without matching inputs here (only copper ore
        // and ingots exist and smelters start on iron), but copper smelting
        // could: compare in "ore equivalents".
        UndoRecord rec = w.takeRecord();
        if (!rec.empty()) undoStack.push_back(std::move(rec));
        auto now = worldValue(w, p);
        auto eq = [](std::map<ItemId, int> m) {
            m[COPPER_ORE] += 3 * m[COPPER_INGOT];
            m.erase(COPPER_INGOT);
            return m;
        };
        if (eq(now) != eq(start)) {
            CHECK(false, "items changed at step %d (op %d)", step, op);
            for (auto& kv : eq(start)) std::printf("    %s: %d -> %d\n", itemDef(kv.first).id.c_str(), kv.second, eq(now)[kv.first]);
            return;
        }
    }
    ++g_passed;
    std::printf("  conservation fuzz: %d builds, %d deletes, %d undos, 6000 steps, no item created or lost\n", built, deleted, undone);
    CHECK(undone > 50, "fuzz should undo things (%d)", undone);
}

// ---------------------------------------------------------------------------
// Undo puts back exactly what was there, contents included.
static void testUndo() {
    World w; w.seed = 1;
    Player p; p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 500);
    p.inventory.tryAdd(COPPER_INGOT, 200);
    Builder b(w, p);
    w.recordChanges = true;
    auto before = asMap(p.inventory.contents());
    // build -> undo: gone, paid back
    CHECK(b.placeMachine(MachineType::Smelter, {1, 1}, Dir::Right, false, nullptr) >= 0, "place smelter");
    UndoRecord r1 = w.takeRecord();
    CHECK(b.undo(r1) == Builder::UndoResult::Ok && w.machineAt({1, 1}) == nullptr && asMap(p.inventory.contents()) == before,
          "undo build");
    // delete a full chest -> undo: chest and its contents are back, inventory as before the delete
    int st = w.addMachine(makeMachine(MachineType::Storage, {10, 0}));
    w.machine(st).storage.tryAdd(COPPER_ORE, 37);
    w.takeRecord();
    auto invBefore = asMap(p.inventory.contents());
    CHECK(b.deleteMachineAt({10, 0}), "delete chest");
    UndoRecord r2 = w.takeRecord();
    CHECK(b.undo(r2) == Builder::UndoResult::Ok, "undo delete");
    Machine* chest = w.machineAt({10, 0});
    CHECK(chest && chest->isStorage() && chest->storage.amount(COPPER_ORE) == 37 && asMap(p.inventory.contents()) == invBefore,
          "undo delete restores contents");
    // belts turned in place with Shift (item kept) -> undo turns them back
    b.placeBelts({0, 5}, {4, 5}, Dir::Right, false);
    w.beltAt({2, 5})->item = IRON_ORE;
    w.takeRecord();
    b.placeBelts({0, 5}, {4, 5}, Dir::Left, true);
    UndoRecord r3 = w.takeRecord();
    CHECK(w.beltAt({2, 5})->dir == Dir::Left, "belts turned");
    CHECK(b.undo(r3) == Builder::UndoResult::Ok && w.beltAt({2, 5})->dir == Dir::Right && w.beltAt({2, 5})->item == IRON_ORE,
          "undo turn keeps the item");
    // something built on the spot since -> refused, nothing changes
    CHECK(b.deleteBeltAt({0, 5}, false), "delete belt");
    UndoRecord r4 = w.takeRecord();
    b.placeMachine(MachineType::Splitter, {0, 5}, Dir::Up, false, nullptr);
    w.takeRecord();
    auto inv4 = asMap(p.inventory.contents());
    CHECK(b.undo(r4) == Builder::UndoResult::Blocked && asMap(p.inventory.contents()) == inv4, "undo into a blocked spot");
    // redo: the inverse record puts it back again, and undo of that again
    {
        World rw; rw.seed = 1;
        Player rp; rp.setCenter(-5000, -5000);
        rp.inventory.tryAdd(IRON_INGOT, 200);
        Builder rb(rw, rp);
        rw.recordChanges = true;
        int sid = rw.addMachine(makeMachine(MachineType::Storage, {0, 0}));
        rw.machine(sid).storage.tryAdd(COPPER_ORE, 12);
        rw.takeRecord();
        auto inv0 = asMap(rp.inventory.contents());
        rb.deleteMachineAt({0, 0});
        UndoRecord del = rw.takeRecord(), redo, undoAgain;
        CHECK(rb.undo(del, nullptr, &redo) == Builder::UndoResult::Ok && rw.machineAt({0, 0}), "undo for redo");
        CHECK(rb.undo(redo, nullptr, &undoAgain) == Builder::UndoResult::Ok && !rw.machineAt({0, 0}), "redo deletes again");
        CHECK(rb.undo(undoAgain) == Builder::UndoResult::Ok && rw.machineAt({0, 0}) && rw.machineAt({0, 0})->storage.amount(COPPER_ORE) == 12 &&
              asMap(rp.inventory.contents()) == inv0, "undo / redo / undo is exact");
    }
    // a miner lying on its side: 3 x 2, saved and turned with blueprints
    {
        World mw; mw.seed = 1;
        auto miner = makeMachine(MachineType::Miner, {0, 0}, Dir::Down, IRON_ORE);
        CHECK(miner->w == 3 && miner->h == 2 && miner->dir == Dir::Down, "flat miner footprint");
        CHECK(makeMachine(MachineType::Smelter, {0, 0}, Dir::Down)->dir == Dir::Right, "square machines have one direction");
        Blueprint bp;
        BlueprintMachine bm; bm.type = MachineType::Miner; bm.off = {0, 0}; bm.dir = Dir::Right;
        bp.machines.push_back(bm); bp.w = 2; bp.h = 3;
        Blueprint r = rotatedCW(bp);
        CHECK(r.w == 3 && r.h == 2 && r.machines[0].dir == Dir::Down, "a turned blueprint lays the miner down");
    }
    std::printf("  undo: builds, deletes (with contents) and in-place turns come back exactly; redo too\n");
}

// Rotating / mirroring blueprints
static Vec2i vec(int x, int y) { return {x, y}; }
static void testBlueprintTransform() {
    Blueprint bp;
    bp.w = 4; bp.h = 3;
    bp.belts.push_back({{0, 0}, Dir::Right});
    bp.belts.push_back({{1, 0}, Dir::Down});
    BlueprintMachine sp; sp.type = MachineType::Splitter; sp.off = {3, 2}; sp.dir = Dir::Up;
    sp.sideFilters[0].enabled = true; sp.sideFilters[0].slots[0] = COAL;
    bp.machines.push_back(sp);
    Blueprint r = rotatedCW(bp);
    CHECK(r.w == 3 && r.h == 4 && r.belts[0].off == vec(2, 0) && r.belts[0].dir == Dir::Down && r.machines[0].off == vec(0, 3) &&
          r.machines[0].dir == Dir::Right, "rotate once");
    Blueprint r4 = rotatedCW(bp, 4);
    CHECK(r4.w == bp.w && r4.belts[1].off == bp.belts[1].off && r4.machines[0].off == bp.machines[0].off, "four turns = original");
    Blueprint m = mirrored(bp);
    CHECK(m.belts[0].off == vec(3, 0) && m.belts[0].dir == Dir::Left && m.machines[0].off == vec(0, 2) &&
          m.machines[0].sideFilters[2].enabled && !m.machines[0].sideFilters[0].enabled, "mirror swaps sides and filters");
    CHECK(m.cost() == bp.cost() && r.cost() == bp.cost(), "same cost");
    std::printf("  blueprints turn and mirror (splitter filters swap sides)\n");
}

// ---------------------------------------------------------------------------
static void testHandCraftingAndRecipeSwitch() {
    Player p;
    // Fill the whole inventory: crafting must refuse when output can't fit.
    for (int i = 0; i < 44; ++i) p.inventory.tryAdd(IRON_INGOT, 100);
    p.inventory.tryAdd(IRON_ORE, 99);
    const Recipe* ingot = findRecipe("iron_ingot");
    // 3 ore -> 1 ingot: inputs removed frees nothing (ore stack 99->96) and the
    // ingot stacks are full -> no space
    CHECK(craftStatus(p.inventory, ingot) == CraftStatus::NoSpace, "craft should report no space");
    CHECK(!tryCraft(p.inventory, ingot), "craft with no space must fail");
    CHECK(p.inventory.amount(IRON_ORE) == 99, "failed craft changed the inventory");

    // Recipe switch refusing when the player can't take the contents back.
    World w;
    w.seed = 1;
    Builder b(w, p);
    int id = w.addMachine(makeMachine(MachineType::Smelter, {0, 0}));
    w.machine(id).inputBuffer(IRON_ORE)->inv.tryAdd(IRON_ORE, 50);
    CHECK(!b.setRecipe(w.machine(id), findRecipe("copper_ingot")), "recipe switch should be refused (no room)");
    CHECK(w.machine(id).inputBuffer(IRON_ORE)->inv.amount(IRON_ORE) == 50, "refused switch lost items");
    p.inventory.tryRemove(IRON_INGOT, 500);
    CHECK(b.setRecipe(w.machine(id), findRecipe("copper_ingot")), "recipe switch should work now");
    CHECK(p.inventory.amount(IRON_ORE) == 149, "switch didn't refund the ore");
}

// ---------------------------------------------------------------------------
// Every 2nd tile in [-256, 256)^2 for seed 424242 must match what the
// original Python terrain.py produced (the file lists the non-grass tiles).
static void testTerrainMatchesPython(const std::map<std::pair<int, int>, int>& expected) {
    int bad = 0, checked = 0, ore = 0;
    for (int y = -256; y < 256; y += 2)
        for (int x = -256; x < 256; x += 2) {
            auto it = expected.find({x, y});
            int want = it == expected.end() ? 0 : it->second;
            int got = terrain::tileTypeAt(424242, x, y);
            ++checked;
            ore += got != 0;
            if (got != want) ++bad;
        }
    CHECK(bad == 0, "%d of %d terrain tiles differ from Python", bad, checked);
    std::printf("  terrain: %d tiles (%d ore) identical to the Python generator\n", checked - bad, ore);
}

static void testSaveRoundTrip() {
    save::setSavesDir("test_saves");
    World w; Player p; CameraState cam;
    save::newGame(w, p, cam, "roundtrip");
    p.inventory.tryAdd(IRON_INGOT, 500);   // new games start empty; enough to build the test layout
    p.inventory.tryAdd(COPPER_INGOT, 200);
    Builder b(w, p);
    b.placeBelts({3, 3}, {10, 3}, Dir::Right, false);
    b.placeMachine(MachineType::Splitter, {11, 3}, Dir::Right, false, nullptr);
    b.placeMachine(MachineType::Storage, {3, 8}, Dir::Right, false, nullptr);
    w.beltAt({4, 3})->item = IRON_INGOT;
    w.beltAt({4, 3})->progress = 7;
    w.machineAt({3, 8})->storage.tryAdd(COAL, 42);
    auto before = worldValue(w, p);
    CHECK(save::saveGame(w, p, cam, "roundtrip"), "save failed");
    World w2; Player p2; CameraState cam2;
    std::string err;
    CHECK(save::loadGame(w2, p2, cam2, "roundtrip", &err), "load failed: %s", err.c_str());
    CHECK(worldValue(w2, p2) == before, "save/load changed the items");
    CHECK(w2.beltAt({4, 3}) && w2.beltAt({4, 3})->progress == 7, "belt progress not saved");
}

static void testLoadPythonSave(const char* path) {
    if (!path) return;
    std::string dir(path);
    size_t slash = dir.find_last_of("/\\");
    std::string name = dir.substr(slash + 1);
    dir = dir.substr(0, slash);
    name = name.substr(0, name.size() - 5);
    save::setSavesDir(dir);
    World w; Player p; CameraState cam;
    std::string err;
    bool ok = save::loadGame(w, p, cam, name, &err);
    CHECK(ok, "could not load python save: %s", err.c_str());
    int belts = 0; for (auto& b : w.belts()) belts += b.alive;
    int machines = 0; for (size_t i = 0; i < w.machineSlots(); ++i) machines += w.machineAlive(int(i));
    std::printf("  python save '%s': %d belts, %d machines, %d items in inventory\n", name.c_str(), belts, machines,
                total(p.inventory.contents()));
    for (int t = 0; t < 600; ++t) w.tick();
}


// ---------------------------------------------------------------------------
static void testBlueprints() {
    World w; w.seed = 1;
    Player p;
    p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 2000);
    p.inventory.tryAdd(COPPER_INGOT, 900);
    Builder b(w, p);
    // a small factory: belt line into a smelter (copper recipe), a splitter with a filter
    CHECK(b.placeBelts({0, 0}, {4, 0}, Dir::Right, false), "belts");
    int sm = b.placeMachine(MachineType::Smelter, {6, 0}, Dir::Right, false, nullptr);
    CHECK(sm >= 0, "smelter");
    b.setRecipe(w.machine(sm), findRecipe("copper_ingot"));
    int sp = b.placeMachine(MachineType::Splitter, {2, 3}, Dir::Down, false, nullptr);
    w.machine(sp).sideFilters[1].enabled = true;
    w.machine(sp).sideFilters[1].slots[0] = COAL;
    w.beltAt({1, 0})->filter.enabled = true;
    w.beltAt({1, 0})->filter.slots[0] = IRON_ORE;
    w.beltAt({1, 0})->item = IRON_ORE;   // items on belts are NOT copied

    Blueprint bp = captureBlueprint(w, {0, 0}, {6, 3});   // smelter only partly inside
    CHECK(bp.belts.size() == 5 && bp.machines.size() == 2, "captured %zu belts %zu machines", bp.belts.size(), bp.machines.size());
    CHECK(bp.w == 8 && bp.h == 5, "blueprint size %dx%d", bp.w, bp.h);  // smelter sticks out above

    auto before = worldValue(w, p);
    auto inv0 = p.inventory.contents();
    Vec2i tl{20, 10};
    CHECK(b.checkBlueprint(bp, tl).status == PlaceStatus::Ok, "should fit");
    CHECK(b.placeBlueprint(bp, tl), "place blueprint");
    // value is conserved: inventory paid exactly the build cost
    CHECK(worldValue(w, p) == before, "placing a blueprint changed the item totals");
    const Machine* copy = w.machineAt(tl + Vec2i{5, 0});
    CHECK(copy && copy->type == MachineType::Smelter && copy->recipe && copy->recipe->id == "copper_ingot", "recipe not copied");
    const Machine* spl = w.machineAt(tl + Vec2i{2, 4});
    CHECK(spl && spl->isSplitter() && spl->dir == Dir::Down && spl->sideFilters[1].enabled && spl->sideFilters[1].slots[0] == COAL, "splitter not copied");
    const Belt* fb = w.beltAt(tl + Vec2i{1, 1});
    CHECK(fb && fb->filter.enabled && fb->filter.slots[0] == IRON_ORE && fb->item == NO_ITEM, "belt filter not copied / item copied");

    // blocked: same place again -> nothing happens
    auto mid = worldValue(w, p);
    auto midInv = p.inventory.contents();
    {   // on top of itself: identical objects are kept, nothing is paid
        auto before = worldValue(w, p);
        int ironBefore = p.inventory.amount(IRON_INGOT);
        CHECK(b.checkBlueprint(bp, tl).status == PlaceStatus::Ok, "identical objects should not block");
        CHECK(b.placeBlueprint(bp, tl), "placing on top of itself");
        CHECK(p.inventory.amount(IRON_INGOT) == ironBefore && worldValue(w, p) == before, "re-placing identical objects changed items");
        CHECK(b.checkBlueprint(bp, tl + Vec2i{1, 0}).status == PlaceStatus::Blocked, "shifted by one should be blocked");
    }
    CHECK(p.inventory.contents() == midInv && worldValue(w, p) == mid, "failed placement changed something");

    // not enough items -> nothing happens
    Player poor; poor.setCenter(-5000, -5000);
    poor.inventory.tryAdd(IRON_INGOT, 20);
    Builder pb(w, poor);
    CHECK(pb.checkBlueprint(bp, {50, 50}).status == PlaceStatus::NoFunds, "should be no funds");
    CHECK(!pb.placeBlueprint(bp, {50, 50}) && poor.inventory.amount(IRON_INGOT) == 20, "placed without paying");

    // blueprint file round trip
    save::setSavesDir("test_saves");
    bp.name = "Test BP";
    CHECK(save::saveBlueprints({bp}), "save blueprints");
    auto loaded = save::loadBlueprints();
    CHECK(loaded.size() == 1 && loaded[0].name == "Test BP" && loaded[0].belts.size() == 5 && loaded[0].machines.size() == 2 &&
          loaded[0].cost() == bp.cost(), "blueprint file round trip");
    // books: kept with their blueprints; a blueprint whose book is missing goes outside
    Blueprint inBook = bp, orphan = bp;
    inBook.name = "In book"; inBook.book = 7;
    orphan.name = "Orphan"; orphan.book = 9;
    CHECK(save::saveBlueprints({bp, inBook, orphan}, {{7, "My book"}}), "save blueprints with a book");
    std::vector<BlueprintBook> books;
    loaded = save::loadBlueprints(&books);
    CHECK(books.size() == 1 && books[0].id == 7 && books[0].name == "My book" && loaded.size() == 3 && loaded[0].book == 0 &&
          loaded[1].book == 7 && loaded[2].book == 0, "blueprint books round trip");
    // reach: only the player's chunk and 4 chunks around it (exact chunk borders)
    {
        World rw; rw.seed = 1;
        Player rp; rp.setCenter(5 * CELL_SIZE + 1, 5 * CELL_SIZE + 1);   // in chunk (0, 0)
        rp.inventory.tryAdd(IRON_INGOT, 1000);
        rp.inventory.tryAdd(COPPER_INGOT, 1000);
        Builder rb(rw, rp);
        rb.blueprintReach = 4;
        Blueprint line;
        line.w = 3; line.h = 1;
        for (int i = 0; i < 3; ++i) line.belts.push_back({{i, 0}, Dir::Right});
        int edge = 5 * CHUNK_SIZE;   // first tile outside, to the right
        CHECK(rb.checkBlueprint(line, {edge - 3, 0}).status == PlaceStatus::Ok, "blueprint at the edge of the reach");
        CHECK(rb.checkBlueprint(line, {edge - 2, 0}).status == PlaceStatus::OutOfReach, "blueprint sticking out of the reach");
        CHECK(rb.checkBlueprint(line, {-4 * CHUNK_SIZE, -4 * CHUNK_SIZE}).status == PlaceStatus::Ok &&
              rb.checkBlueprint(line, {-4 * CHUNK_SIZE - 1, 0}).status == PlaceStatus::OutOfReach, "reach on the left / top");
        CHECK(!rb.placeBlueprint(line, {edge, 0}) && rw.beltAt({edge, 0}) == nullptr, "placed outside the reach");
        CHECK(rb.placeMachine(MachineType::Smelter, {edge + 10, 0}, Dir::Right, false, nullptr) >= 0, "single machines aren't limited");
    }
    std::printf("  blueprint: %zu belts + %zu machines copied, cost paid exactly, blocked/no-funds placements change nothing\n",
                bp.belts.size(), bp.machines.size());
}


// ---------------------------------------------------------------------------
static void testAreaDelete() {
    World w; w.seed = 1;
    Player p; p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 1000);
    p.inventory.tryAdd(COPPER_INGOT, 300);
    Builder b(w, p);
    b.placeBelts({0, 0}, {5, 0}, Dir::Right, false);
    int st = b.placeMachine(MachineType::Storage, {8, 1}, Dir::Right, false, nullptr);
    w.machine(st).storage.tryAdd(COAL, 250);
    w.beltAt({2, 0})->item = IRON_ORE;
    auto before = worldValue(w, p);
    ItemAmounts got; int n = 0;
    CHECK(b.deleteArea({1, -1}, {7, 0}, &got, &n) == Builder::AreaDelete::Ok, "area delete");
    CHECK(n == 6, "deleted %d objects, expected 6 (5 belts + storage)", n);   // storage at 7..8 sticks in
    CHECK(worldValue(w, p) == before, "area delete changed item totals");
    CHECK(p.inventory.amount(COAL) == 250 && p.inventory.amount(IRON_ORE) == 1, "contents not refunded");
    CHECK(w.beltAt({0, 0}) != nullptr && w.beltAt({1, 0}) == nullptr, "wrong belts deleted");
    CHECK(b.deleteArea({20, 20}, {25, 25}) == Builder::AreaDelete::Nothing, "empty area");

    // not enough space: nothing happens
    Player full; full.setCenter(-5000, -5000);
    for (int i = 0; i < 45; ++i) full.inventory.tryAdd(COAL, 100);
    Builder fb(w, full);
    auto mid = worldValue(w, full);
    CHECK(fb.deleteArea({-1, -1}, {1, 1}) == Builder::AreaDelete::NoSpace, "should be no space");
    CHECK(w.beltAt({0, 0}) != nullptr && worldValue(w, full) == mid, "failed area delete changed something");
    std::printf("  area delete: refunds build cost + contents, nothing happens without space\n");
}

static bool itemDeltaEmpty(const ItemAmounts& d) {
    for (auto& x : d) if (x.second != 0) return false;
    return true;
}

// Identical objects underneath are kept; Shift replaces, keeping belt items and
// machine contents unless a recipe actually changes.
static void testPlaceInPlace() {
    World w; w.seed = 1;
    Player p; p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 2000);
    p.inventory.tryAdd(COPPER_INGOT, 1000);
    Builder b(w, p);
    // same machine: allowed without Shift, free, contents stay
    int sm = b.placeMachine(MachineType::Smelter, {5, 5}, Dir::Right, false, nullptr);
    MachineBuffer* in = w.machine(sm).inputBuffer(w.machine(sm).inputs[0].item);
    in->inv.tryAdd(in->item, 7);
    auto v0 = worldValue(w, p);
    ItemAmounts d;
    CHECK(b.placeMachine(MachineType::Smelter, {5, 5}, Dir::Right, false, nullptr, &d) == sm, "same machine should be kept");
    CHECK(itemDeltaEmpty(d) && w.machine(sm).inputs[0].inv.amount(in->item) == 7 && worldValue(w, p) == v0, "same machine changed items");
    // a different machine there: blocked without Shift
    CHECK(b.placeMachine(MachineType::Assembler, {5, 5}, Dir::Right, false, nullptr) < 0, "different machine should block");
    // splitter turned with Shift: same object, keeps its item
    int sp = b.placeMachine(MachineType::Splitter, {20, 5}, Dir::Right, false, nullptr);
    w.machine(sp).item = IRON_ORE;
    CHECK(b.placeMachine(MachineType::Splitter, {20, 5}, Dir::Down, false, nullptr) < 0, "turned splitter needs Shift");
    CHECK(b.placeMachine(MachineType::Splitter, {20, 5}, Dir::Down, true, nullptr) == sp && w.machine(sp).dir == Dir::Down &&
          w.machine(sp).item == IRON_ORE, "splitter not turned in place / item lost");
    // belts turned with Shift keep their item and cost nothing
    b.placeBelts({0, 10}, {4, 10}, Dir::Right, false);
    w.beltAt({2, 10})->item = COAL;
    int iron = p.inventory.amount(IRON_INGOT);
    CHECK(b.placeBelts({2, 8}, {2, 12}, Dir::Down, true), "shift belts over belts");
    CHECK(w.beltAt({2, 10})->dir == Dir::Down && w.beltAt({2, 10})->item == COAL, "belt item lost when turned");
    CHECK(p.inventory.amount(IRON_INGOT) == iron - 4 * 10, "only the 4 new belts should cost (paid %d)", iron - p.inventory.amount(IRON_INGOT));
    // blueprint over the smelter: same recipe keeps contents, other recipe hands them out
    Blueprint bp = captureBlueprint(w, {5, 5}, {5, 5});
    auto v1 = worldValue(w, p);
    CHECK(b.placeBlueprint(bp, w.machine(sm).pos) && w.machine(sm).inputs[0].inv.amount(in->item) == 7 && worldValue(w, p) == v1,
          "same recipe should keep the contents");
    bp.machines[0].recipeId = "copper_ingot";
    ItemId oldItem = w.machine(sm).inputs[0].item;
    int had = p.inventory.amount(oldItem);
    CHECK(b.placeBlueprint(bp, w.machine(sm).pos) && w.machine(sm).recipe->id == "copper_ingot", "recipe not changed in place");
    CHECK(p.inventory.amount(oldItem) == had + 7 && worldValue(w, p) == v1, "recipe change should hand out the old contents");
    // the game only builds in explored chunks
    b.requireExplored = true;
    Blueprint far = captureBlueprint(w, {5, 5}, {5, 5});
    CHECK(b.checkBlueprint(far, {4000, 4000}).status == PlaceStatus::Undiscovered && !b.placeBlueprint(far, {4000, 4000}),
          "placed in an undiscovered chunk");
    CHECK(!w.isExplored(4000 / CHUNK_SIZE, 4000 / CHUNK_SIZE), "checking explored the chunk");
    b.requireExplored = false;
    std::printf("  identical objects kept, Shift turns/replaces, items kept unless the recipe changes\n");
}

// Selections are at most MAX_SELECTION x MAX_SELECTION; a machine that would
// make the result bigger (touching the edge of a full-size selection) is left out.
static void testSelectionLimit() {
    Selection s = makeSelection({0, 0}, {500, -500});
    CHECK(s.w() == MAX_SELECTION && s.h() == MAX_SELECTION && s.x0 == 0 && s.y1 == 0, "selection not clamped: %dx%d", s.w(), s.h());
    World w; w.seed = 1;
    Player p; p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 2000);
    p.inventory.tryAdd(COPPER_INGOT, 1000);
    Builder b(w, p);
    const int M = MAX_SELECTION;
    int inside = b.placeMachine(MachineType::Smelter, {10, 10}, Dir::Right, false, nullptr);         // fully inside
    int edge = b.placeMachine(MachineType::Smelter, {M, 10}, Dir::Right, false, nullptr);            // 1 column inside, rest outside
    b.placeBelts({0, 0}, {3, 0}, Dir::Right, false);
    CHECK(inside >= 0 && edge >= 0, "setup");
    std::vector<int> belts, machines;
    gatherSelection(w, makeSelection({0, 0}, {M - 1, M - 1}), belts, machines);   // full size
    bool hasEdge = std::find(machines.begin(), machines.end(), edge) != machines.end();
    CHECK(belts.size() == 4 && machines.size() == 1 && !hasEdge, "full-size selection took the edge machine");
    gatherSelection(w, makeSelection({M - 5, 0}, {M - 1, 20}), belts, machines);   // small: sticking out is fine
    hasEdge = std::find(machines.begin(), machines.end(), edge) != machines.end();
    CHECK(hasEdge, "small selection should take a machine that sticks out");
    Blueprint bp = captureBlueprint(w, {0, 0}, {M + 50, M + 50});
    CHECK(bp.w <= M && bp.h <= M, "blueprint too big: %dx%d", bp.w, bp.h);
    std::printf("  selections: max %dx%d, edge machines left out\n", M, M);
}

// The "+1 / -2" messages: every action reports exactly how the inventory changed.
static void testItemDeltas() {
    World w; w.seed = 1;
    Player p; p.setCenter(-5000, -5000);
    p.inventory.tryAdd(IRON_INGOT, 1000);
    p.inventory.tryAdd(COPPER_INGOT, 300);
    Builder b(w, p);
    auto check = [&](const char* what, auto action) {
        int iron = p.inventory.amount(IRON_INGOT), copper = p.inventory.amount(COPPER_INGOT), ore = p.inventory.amount(IRON_ORE);
        ItemAmounts d;
        CHECK(action(d), "%s failed", what);
        int di = 0, dc = 0, dore = 0;
        for (auto& x : d) {
            if (x.first == IRON_INGOT) di += x.second;
            else if (x.first == COPPER_INGOT) dc += x.second;
            else if (x.first == IRON_ORE) dore += x.second;
        }
        CHECK(p.inventory.amount(IRON_INGOT) - iron == di && p.inventory.amount(COPPER_INGOT) - copper == dc &&
              p.inventory.amount(IRON_ORE) - ore == dore, "%s: reported change doesn't match the inventory", what);
    };
    check("place belts", [&](ItemAmounts& d) { return b.placeBelts({0, 0}, {4, 0}, Dir::Right, false, &d); });
    w.beltAt({1, 0})->item = IRON_ORE;
    check("delete belt", [&](ItemAmounts& d) { return b.deleteBeltAt({1, 0}, false, &d); });
    check("delete belt run", [&](ItemAmounts& d) { return b.deleteBeltAt({3, 0}, true, &d); });
    check("place machine", [&](ItemAmounts& d) { return b.placeMachine(MachineType::Smelter, {10, 10}, Dir::Right, false, nullptr, &d) >= 0; });
    b.placeBelts({20, 0}, {24, 0}, Dir::Right, false);
    check("replace belts", [&](ItemAmounts& d) { return b.placeMachine(MachineType::Smelter, {21, 0}, Dir::Right, true, nullptr, &d) >= 0; });
    check("delete machine", [&](ItemAmounts& d) { return b.deleteMachineAt({10, 10}, &d); });
    b.placeBelts({30, 0}, {32, 0}, Dir::Right, false);
    Blueprint bp = captureBlueprint(w, {30, 0}, {32, 0});
    check("place blueprint", [&](ItemAmounts& d) { return b.placeBlueprint(bp, {30, 5}, false, &d); });
    std::printf("  item change messages match the inventory\n");
}

int main(int argc, char** argv) {
    IRON_ORE = itemByStringId("iron_ore");
    IRON_INGOT = itemByStringId("iron_ingot");
    COPPER_INGOT = itemByStringId("copper_ingot");
    COPPER_ORE = itemByStringId("copper_ore");
    COAL = itemByStringId("coal");

    std::printf("Belt throughput\n");
    testStraightBeltThroughput(1);
    testStraightBeltThroughput(10);
    testStraightBeltThroughput(500);
    testStraightBeltThroughput(5000);
    testMerge();
    testSplitter();
    testSplitterIntoMachine();
    testFullLoopRotates();
    std::printf("Machines\n");
    testProductionChain();
    testSideOutputs();
    testTakeContents();
    testPickUpUnderPlayer();
    testHandCraftingAndRecipeSwitch();
    std::printf("Item conservation\n");
    testConservationFuzz();
    testUndo();
    testBlueprintTransform();
    testBlueprints();
    testAreaDelete();
    testItemDeltas();
    testSelectionLimit();
    testPlaceInPlace();
    std::printf("Terrain & saves\n");
    // expected terrain samples are read from argv[1] if given: lines "x y type"
    if (argc > 1) {
        std::map<std::pair<int, int>, int> exp;
        FILE* f = std::fopen(argv[1], "r");
        int x, y, t;
        while (f && std::fscanf(f, "%d %d %d", &x, &y, &t) == 3) exp[{x, y}] = t;
        if (f) std::fclose(f);
        testTerrainMatchesPython(exp);
    }
    testSaveRoundTrip();
    testLoadPythonSave(argc > 2 ? argv[2] : nullptr);

    std::printf("\n%d checks passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
