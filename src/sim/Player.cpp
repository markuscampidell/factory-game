// sim/Player.cpp
#include "Player.hpp"
#include "World.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

std::vector<const Machine*> machinesNear(World& w, const RectF& r) {
    std::vector<const Machine*> out;
    int left = int(std::floor(r.left() / CELL_SIZE)) - 3;
    int top = int(std::floor(r.top() / CELL_SIZE)) - 3;
    int right = int(std::floor((r.right() - 1) / CELL_SIZE)) + 3;
    int bottom = int(std::floor((r.bottom() - 1) / CELL_SIZE)) + 3;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x) {
            const Machine* m = w.machineAt({x, y});
            if (m && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
        }
    return out;
}

} // namespace

void Player::update(World& world, int inX, int inY) {
    // Same feel as the Python version: accelerate, then apply friction.
    dx = (dx + float(inX) * SPEED) * FRICTION;
    dy = (dy + float(inY) * SPEED) * FRICTION;
    if (std::fabs(dx) < 0.01f) dx = 0;
    if (std::fabs(dy) < 0.01f) dy = 0;

    if (dx != 0) {
        rect.x += dx;
        for (const Machine* m : machinesNear(world, rect)) {
            RectF mr = m->pixelRect();
            if (rect.intersects(mr)) {
                if (dx > 0) rect.x = mr.left() - rect.w;
                else rect.x = mr.right();
                dx = 0;
            }
        }
    }
    if (dy != 0) {
        rect.y += dy;
        for (const Machine* m : machinesNear(world, rect)) {
            RectF mr = m->pixelRect();
            if (rect.intersects(mr)) {
                if (dy > 0) rect.y = mr.top() - rect.h;
                else rect.y = mr.bottom();
                dy = 0;
            }
        }
    }
}

bool Player::coversTile(Vec2i t) const {
    int left = int(std::floor(rect.left() / CELL_SIZE));
    int right = int(std::floor((rect.right() - 1) / CELL_SIZE));
    int top = int(std::floor(rect.top() / CELL_SIZE));
    int bottom = int(std::floor((rect.bottom() - 1) / CELL_SIZE));
    return left <= t.x && t.x <= right && top <= t.y && t.y <= bottom;
}

const Recipe* selectedHandRecipe(const Player& p) {
    const auto& list = handcraftRecipes();
    if (list.empty()) return nullptr;
    int i = std::clamp(p.selectedRecipe, 0, int(list.size()) - 1);
    return list[size_t(i)];
}

CraftStatus craftStatus(const Inventory& inv, const Recipe* r) {
    if (!r || !inv.hasEnough(r->inputs)) return CraftStatus::NoInputs;
    Inventory scratch = inv;
    scratch.tryRemoveAll(r->inputs);
    for (auto& p : r->outputs)
        if (!scratch.tryAdd(p.first, p.second)) return CraftStatus::NoSpace;
    return CraftStatus::Ok;
}

bool tryCraft(Inventory& inv, const Recipe* r) {
    if (craftStatus(inv, r) != CraftStatus::Ok) return false;
    inv.tryRemoveAll(r->inputs);
    for (auto& p : r->outputs) inv.tryAdd(p.first, p.second);
    return true;
}

void updateHandCrafting(Player& p) {
    if (p.craftMode == CraftMode::None) { p.craftTicks = 0; return; }
    const Recipe* r = selectedHandRecipe(p);
    if (!r || craftStatus(p.inventory, r) != CraftStatus::Ok) {
        p.craftMode = CraftMode::None;
        p.craftTicks = 0;
        return;
    }
    ++p.craftTicks;
    if (p.craftTicks >= r->ticks) {
        tryCraft(p.inventory, r);
        p.craftTicks = 0;
        if (p.craftMode == CraftMode::Single || craftStatus(p.inventory, r) != CraftStatus::Ok)
            p.craftMode = CraftMode::None;
    }
}
