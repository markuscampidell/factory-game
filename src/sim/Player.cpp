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

// Does machine m block the player rectangle p? Only where its sprite is
// solid (HitMask); the whole footprint if no mask is registered.
static bool blocks(World& w, const Machine& m, const RectF& p) {
    RectF mr = m.pixelRect();
    if (!p.intersects(mr)) return false;
    int variant = m.isPipe() ? int(w.pipeMask(m.pos)) : int(normalizeMachineDir(m.type, m.dir));
    const HitMask* hm = hitMask(m.type, variant);
    if (!hm) return true;
    float x0 = std::max(p.left(), mr.left()), x1 = std::min(p.right(), mr.right());
    float y0 = std::max(p.top(), mr.top()), y1 = std::min(p.bottom(), mr.bottom());
    // check the overlap at every world pixel (a few hundred lookups at most)
    for (float y = std::floor(y0) + 0.5f; y < y1; y += 1.0f) {
        if (y < y0) continue;
        int my = std::clamp(int((y - mr.top()) * float(hm->h) / mr.h), 0, hm->h - 1);
        for (float x = std::floor(x0) + 0.5f; x < x1; x += 1.0f) {
            if (x < x0) continue;
            int mx = std::clamp(int((x - mr.left()) * float(hm->w) / mr.w), 0, hm->w - 1);
            if (hm->solid[size_t(my * hm->w + mx)]) return true;
        }
    }
    return false;
}

void Player::update(World& world, int inX, int inY) {
    // Same feel as the Python version: accelerate, then apply friction.
    dx = (dx + float(inX) * SPEED) * FRICTION;
    dy = (dy + float(inY) * SPEED) * FRICTION;
    if (std::fabs(dx) < 0.01f) dx = 0;
    if (std::fabs(dy) < 0.01f) dy = 0;

    // Wading through water is 50% slower (the speed itself is kept, so you
    // are at full speed again as soon as you step out).
    Vec2i here{int(std::floor(rect.cx() / CELL_SIZE)), int(std::floor(rect.cy() / CELL_SIZE))};
    float k = world.isWater(here) ? WATER_SPEED : 1.0f;

    auto collides = [&]() {
        for (const Machine* m : machinesNear(world, rect))
            if (blocks(world, *m, rect)) return true;
        return false;
    };
    // Moves one axis by s; if that runs into something, goes back and creeps
    // up to it a pixel at a time (machines can have any shape now).
    auto moveAxis = [&](float& pos, float& vel, float& s) {
        pos += s;
        if (!collides()) return;
        pos -= s;
        float sign = s > 0 ? 1.0f : -1.0f, left = std::fabs(s);
        while (left > 0) {
            float st = std::min(1.0f, left);
            pos += sign * st;
            if (collides()) { pos -= sign * st; break; }
            left -= st;
        }
        vel = s = 0;
    };

    // Move in small steps (at most a quarter tile each), so even a very
    // fast player can't jump over a machine between two checks.
    const float maxStep = CELL_SIZE / 4.0f;
    int steps = int(std::ceil(std::max(std::fabs(dx * k), std::fabs(dy * k)) / maxStep));
    float sx = steps > 0 ? dx * k / float(steps) : 0, sy = steps > 0 ? dy * k / float(steps) : 0;
    for (int i = 0; i < steps; ++i) {
        if (sx != 0) moveAxis(rect.x, dx, sx);
        if (sy != 0) moveAxis(rect.y, dy, sy);
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
