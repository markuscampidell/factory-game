// core/Types.hpp - small value types shared by the whole game.
#pragma once
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Simulation constants
// ---------------------------------------------------------------------------
// The whole simulation runs on a fixed tick. Everything that moves or takes
// time is counted in whole ticks (integers), never in floating point seconds,
// so nothing can ever drift, round away or depend on the frame rate.
constexpr int TICKS_PER_SECOND = 60;

// One belt tile is crossed in exactly this many ticks -> 60/15 = 4 tiles/s,
// the same speed as the Python version. A fully packed belt therefore always
// delivers exactly 4 items per second, no matter how long it is.
constexpr int BELT_TICKS_PER_TILE = 15;

constexpr int CELL_SIZE = 32;   // pixels per tile
constexpr int CHUNK_SIZE = 32;  // tiles per chunk edge
constexpr int CHUNK_SHIFT = 5;  // CHUNK_SIZE == 1 << CHUNK_SHIFT (fast tile -> chunk math)
static_assert(CHUNK_SIZE == 1 << CHUNK_SHIFT, "CHUNK_SIZE must be a power of two");

// ---------------------------------------------------------------------------
// Integer 2D vector (tile coordinates, directions)
// ---------------------------------------------------------------------------
struct Vec2i {
    int x = 0;
    int y = 0;
    constexpr Vec2i() = default;
    constexpr Vec2i(int x_, int y_) : x(x_), y(y_) {}
    constexpr Vec2i operator+(Vec2i o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2i operator-(Vec2i o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2i operator-() const { return {-x, -y}; }
    constexpr Vec2i operator*(int s) const { return {x * s, y * s}; }
    constexpr bool operator==(Vec2i o) const { return x == o.x && y == o.y; }
    constexpr bool operator!=(Vec2i o) const { return !(*this == o); }
};

struct Vec2iHash {
    size_t operator()(Vec2i v) const noexcept {
        uint64_t k = (uint64_t(uint32_t(v.x)) << 32) | uint32_t(v.y);
        k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33;
        return size_t(k);
    }
};

// Python-style floor division (rounds toward -infinity), needed so negative
// tile coordinates map to the right chunk (-1 -> chunk -1, not 0).
constexpr int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
constexpr int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

// ---------------------------------------------------------------------------
// Directions. Numbered clockwise so "rotate clockwise" is just +1.
// ---------------------------------------------------------------------------
enum class Dir : uint8_t { Right = 0, Down = 1, Left = 2, Up = 3 };

constexpr Vec2i dirVec(Dir d) {
    switch (d) {
        case Dir::Right: return {1, 0};
        case Dir::Down:  return {0, 1};
        case Dir::Left:  return {-1, 0};
        default:         return {0, -1};
    }
}
constexpr Dir rotateCW(Dir d, int steps = 1) {
    return Dir(uint8_t(floorMod(int(d) + steps, 4)));
}
constexpr Dir opposite(Dir d) { return rotateCW(d, 2); }
constexpr uint8_t dirBit(Dir d) { return uint8_t(1u << uint8_t(d)); }

inline bool dirFromVec(Vec2i v, Dir& out) {
    if (v == Vec2i{1, 0})  { out = Dir::Right; return true; }
    if (v == Vec2i{0, 1})  { out = Dir::Down;  return true; }
    if (v == Vec2i{-1, 0}) { out = Dir::Left;  return true; }
    if (v == Vec2i{0, -1}) { out = Dir::Up;    return true; }
    return false;
}

inline const char* dirName(Dir d) {
    switch (d) {
        case Dir::Right: return "right";
        case Dir::Down:  return "down";
        case Dir::Left:  return "left";
        default:         return "up";
    }
}

// Axis-aligned rectangle in world pixels (used for the player and machines).
struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    float left() const { return x; }
    float right() const { return x + w; }
    float top() const { return y; }
    float bottom() const { return y + h; }
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
    bool intersects(const RectF& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};
