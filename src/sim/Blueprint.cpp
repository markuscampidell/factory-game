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
        const MachineSpec& s = machineSpec(m.type);
        bool free = true;
        for (int dy = 0; dy < s.h && free; ++dy)
            for (int dx = 0; dx < s.w && free; ++dx)
                if (tmp.isCellBlocked({m.off.x + dx, m.off.y + dy})) free = false;
        if (free) tmp.addMachine(makeMachine(m.type, m.off, m.dir, itemByStringId("iron_ore")));
    }
    tmp.ensureTopology();
    for (size_t i = 0; i < belts.size(); ++i) belts[i].mask = tmp.belt(ids[i]).incomingMask;
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
