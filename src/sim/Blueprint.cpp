// sim/Blueprint.cpp
#include "Blueprint.hpp"
#include "Build.hpp"
#include <algorithm>
#include <climits>
#include <set>

ItemAmounts Blueprint::cost() const {
    ItemAmounts total;
    for (size_t i = 0; i < belts.size(); ++i)
        for (auto& c : beltBuildCost()) addAmount(total, c.first, c.second);
    for (auto& m : machines)
        for (auto& c : machineSpec(m.type).cost) addAmount(total, c.first, c.second);
    return total;
}

void Blueprint::computeMasks() {
    // Build the blueprint in a scratch world and let it work out how the
    // belts connect, so the placement preview shows the right curves.
    World tmp;
    tmp.seed = 0;
    std::vector<int> ids;
    for (auto& b : belts) ids.push_back(tmp.addBelt(b.off, b.dir));
    for (auto& m : machines) {
        Vec2i s = machineSize(m.type, m.dir);
        bool free = true;
        for (int dy = 0; dy < s.y && free; ++dy)
            for (int dx = 0; dx < s.x && free; ++dx)
                if (tmp.isCellBlocked({m.off.x + dx, m.off.y + dy})) free = false;
        if (free) tmp.addMachine(makeMachine(m.type, m.off, m.dir, itemByStringId("iron_ore")));
    }
    tmp.ensureTopology();
    for (size_t i = 0; i < belts.size(); ++i) belts[i].mask = tmp.belt(ids[i]).incomingMask;
}

// Shifts everything so the smallest offset is 0 and recomputes w / h.
static void normalize(Blueprint& bp) {
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    auto grow = [&](Vec2i p, int w, int h) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x + w - 1); y1 = std::max(y1, p.y + h - 1);
    };
    for (auto& b : bp.belts) grow(b.off, 1, 1);
    for (auto& m : bp.machines) grow(m.off, machineSize(m.type, m.dir).x, machineSize(m.type, m.dir).y);
    if (x0 == INT_MAX) { bp.w = bp.h = 0; return; }
    for (auto& b : bp.belts) b.off = b.off - Vec2i{x0, y0};
    for (auto& m : bp.machines) m.off = m.off - Vec2i{x0, y0};
    bp.w = x1 - x0 + 1;
    bp.h = y1 - y0 + 1;
}

Blueprint rotatedCW(const Blueprint& src, int turns) {
    turns = floorMod(turns, 4);
    Blueprint bp = src;
    for (int t = 0; t < turns; ++t) {
        Blueprint out = bp;
        const int H = bp.h;
        // tile (x, y) -> (H - 1 - y, x)
        for (auto& b : out.belts) {
            b.off = {H - 1 - b.off.y, b.off.x};
            b.dir = rotateCW(b.dir, 1);
        }
        for (auto& m : out.machines) {
            // footprint w x h at (x, y) -> h x w at (H - y - h, x); a miner
            // simply lies down / stands up
            Vec2i s = machineSize(m.type, m.dir);
            m.off = {H - m.off.y - s.y, m.off.x};
            m.dir = normalizeMachineDir(m.type, rotateCW(m.dir, 1));
        }
        normalize(out);
        bp = out;
    }
    bp.computeMasks();
    return bp;
}

Blueprint mirrored(const Blueprint& src) {
    Blueprint bp = src;
    auto flip = [](Dir d) { return d == Dir::Left ? Dir::Right : d == Dir::Right ? Dir::Left : d; };
    for (auto& b : bp.belts) {
        b.off.x = src.w - 1 - b.off.x;
        b.dir = flip(b.dir);
    }
    for (auto& m : bp.machines) {
        m.off.x = src.w - m.off.x - machineSize(m.type, m.dir).x;
        m.dir = mirroredMachineDir(m.type, m.dir);
        if (m.type == MachineType::Splitter) std::swap(m.sideFilters[0], m.sideFilters[2]);   // left <-> right
    }
    normalize(bp);
    bp.computeMasks();
    return bp;
}

Selection makeSelection(Vec2i start, Vec2i end) {
    const int m = MAX_SELECTION - 1;
    Vec2i e{std::clamp(end.x, start.x - m, start.x + m), std::clamp(end.y, start.y - m, start.y + m)};
    Selection s;
    s.x0 = std::min(start.x, e.x); s.x1 = std::max(start.x, e.x);
    s.y0 = std::min(start.y, e.y); s.y1 = std::max(start.y, e.y);
    return s;
}

void gatherSelection(const World& w, const Selection& s, std::vector<int>& belts, std::vector<int>& machines) {
    belts.clear();
    machines.clear();
    std::set<int> seen;
    // area covered so far: starts as the selection, grows with machines that stick out
    int bx0 = s.x0, by0 = s.y0, bx1 = s.x1, by1 = s.y1;
    for (int y = s.y0; y <= s.y1; ++y)
        for (int x = s.x0; x <= s.x1; ++x) {
            TileRef r = w.refAt({x, y});
            if (r > 0) { belts.push_back(r - 1); continue; }
            if (r == 0 || !seen.insert(-r - 1).second) continue;
            const Machine& m = w.machine(-r - 1);
            int nx0 = std::min(bx0, m.pos.x), ny0 = std::min(by0, m.pos.y);
            int nx1 = std::max(bx1, m.pos.x + m.w - 1), ny1 = std::max(by1, m.pos.y + m.h - 1);
            if (nx1 - nx0 + 1 > MAX_SELECTION || ny1 - ny0 + 1 > MAX_SELECTION) continue;   // too big: leave it
            bx0 = nx0; by0 = ny0; bx1 = nx1; by1 = ny1;
            machines.push_back(-r - 1);
        }
}

Blueprint captureBlueprint(const World& w, Vec2i start, Vec2i end) {
    Blueprint bp;
    std::vector<int> beltIds, machineIds;
    gatherSelection(w, makeSelection(start, end), beltIds, machineIds);
    if (beltIds.empty() && machineIds.empty()) return bp;

    // bounding box of everything taken (machines may stick out)
    int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
    for (int id : beltIds) {
        Vec2i p = w.belt(id).pos;
        minX = std::min(minX, p.x); minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y);
    }
    for (int id : machineIds) {
        const Machine& m = w.machine(id);
        minX = std::min(minX, m.pos.x); minY = std::min(minY, m.pos.y);
        maxX = std::max(maxX, m.pos.x + m.w - 1); maxY = std::max(maxY, m.pos.y + m.h - 1);
    }
    bp.w = maxX - minX + 1;
    bp.h = maxY - minY + 1;
    Vec2i origin{minX, minY};

    for (int id : beltIds) {
        const Belt& src = w.belt(id);
        bp.belts.push_back({src.pos - origin, src.dir, src.filter, 0});
    }
    for (int id : machineIds) {
        const Machine& m = w.machine(id);
        BlueprintMachine bm;
        bm.type = m.type;
        bm.off = m.pos - origin;
        bm.dir = m.dir;
        if (m.recipeChoices() && m.recipe) bm.recipeId = m.recipe->id;
        bm.sideFilters = m.sideFilters;
        bp.machines.push_back(bm);
    }
    bp.computeMasks();
    return bp;
}
