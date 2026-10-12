// sim/Terrain.cpp - see Terrain.hpp. Kept numerically identical to Python.
#include "Terrain.hpp"
#include "../core/Types.hpp"
#include <cmath>
#include <unordered_map>
#include <tuple>

namespace terrain {
namespace {

constexpr int CANDIDATE_CELL_TILES = CHUNK_SIZE;
constexpr int MIN_PATCH_SEPARATION_CHUNKS = 2;
constexpr double PATCH_BASE_RADIUS_TILES = 8;
constexpr double PATCH_RADIUS_JITTER_TILES = 5;
constexpr double PATCH_EDGE_NOISE_SCALE = 8;
constexpr double PATCH_EDGE_NOISE_STRENGTH = 4;

constexpr int64_t SALT_JITTER_X = 101;
constexpr int64_t SALT_JITTER_Y = 103;
constexpr int64_t SALT_TYPE = 107;
constexpr int64_t SALT_RADIUS = 109;
constexpr int64_t SALT_EDGE = 113;
constexpr int64_t SALT_PRIORITY = 127;

// Same integer hash as _hash01 in terrain.py. Python integers are unbounded,
// but every step is masked to 32 bits, and wrapping 64-bit arithmetic gives
// the same low 32 bits, so the results are bit-identical.
double hash01(int64_t seed, int64_t ix, int64_t iy, int64_t salt) {
    uint64_t n = uint64_t(ix) * 374761393ULL + uint64_t(iy) * 668265263ULL +
                 uint64_t(seed) * 2147483647ULL + uint64_t(salt) * 1000003ULL;
    n &= 0xFFFFFFFFULL;
    n = ((n ^ (n >> 13)) * 1274126177ULL) & 0xFFFFFFFFULL;
    n ^= n >> 16;
    return double(n) / double(0xFFFFFFFFULL);
}

double smoothstep(double t) { return t * t * (3 - 2 * t); }

double valueNoise(int64_t seed, double x, double y, int64_t salt, double scale) {
    double lx = x / scale, ly = y / scale;
    int64_t x0 = int64_t(std::floor(lx)), y0 = int64_t(std::floor(ly));
    double tx = smoothstep(lx - double(x0)), ty = smoothstep(ly - double(y0));
    double v00 = hash01(seed, x0, y0, salt);
    double v10 = hash01(seed, x0 + 1, y0, salt);
    double v01 = hash01(seed, x0, y0 + 1, salt);
    double v11 = hash01(seed, x0 + 1, y0 + 1, salt);
    double top = v00 + (v10 - v00) * tx;
    double bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

void candidatePosition(int64_t seed, int64_t ccx, int64_t ccy, double& outX, double& outY) {
    double baseX = double(ccx * CANDIDATE_CELL_TILES);
    double baseY = double(ccy * CANDIDATE_CELL_TILES);
    outX = baseX + hash01(seed, ccx, ccy, SALT_JITTER_X) * CANDIDATE_CELL_TILES;
    outY = baseY + hash01(seed, ccx, ccy, SALT_JITTER_Y) * CANDIDATE_CELL_TILES;
}

// (priority, (ccx, ccy)) compared lexicographically, like the Python tuple.
bool keyGreater(int64_t seed, int64_t ax, int64_t ay, int64_t bx, int64_t by) {
    double pa = hash01(seed, ax, ay, SALT_PRIORITY);
    double pb = hash01(seed, bx, by, SALT_PRIORITY);
    return std::make_tuple(pa, ax, ay) > std::make_tuple(pb, bx, by);
}

struct CacheKey {
    int64_t seed, x, y;
    bool operator==(const CacheKey& o) const { return seed == o.seed && x == o.x && y == o.y; }
};
struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const {
        uint64_t h = uint64_t(k.seed) * 0x9E3779B97F4A7C15ULL;
        h ^= uint64_t(k.x) * 0xC2B2AE3D27D4EB4FULL + (h << 6) + (h >> 2);
        h ^= uint64_t(k.y) * 0x165667B19E3779F9ULL + (h << 6) + (h >> 2);
        return size_t(h);
    }
};
std::unordered_map<CacheKey, bool, CacheKeyHash>& centerCache() {
    static std::unordered_map<CacheKey, bool, CacheKeyHash> cache;
    return cache;
}

bool isPatchCenter(int64_t seed, int64_t ccx, int64_t ccy) {
    auto& cache = centerCache();
    CacheKey key{seed, ccx, ccy};
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    double cx, cy;
    candidatePosition(seed, ccx, ccy, cx, cy);
    const double minSepTiles = MIN_PATCH_SEPARATION_CHUNKS * CHUNK_SIZE;
    const int reach = int(minSepTiles / CANDIDATE_CELL_TILES) + 2;

    bool result = true;
    for (int dcy = -reach; dcy <= reach && result; ++dcy) {
        for (int dcx = -reach; dcx <= reach; ++dcx) {
            if (dcx == 0 && dcy == 0) continue;
            int64_t ocx = ccx + dcx, ocy = ccy + dcy;
            double ox, oy;
            candidatePosition(seed, ocx, ocy, ox, oy);
            if (std::hypot(cx - ox, cy - oy) < minSepTiles && keyGreater(seed, ocx, ocy, ccx, ccy)) {
                result = false;
                break;
            }
        }
    }
    if (cache.size() > 2000000) cache.clear();
    cache.emplace(key, result);
    return result;
}

double patchRadius(int64_t seed, int64_t ccx, int64_t ccy) {
    return PATCH_BASE_RADIUS_TILES + (hash01(seed, ccx, ccy, SALT_RADIUS) - 0.5) * 2 * PATCH_RADIUS_JITTER_TILES;
}

uint8_t patchTileType(int64_t seed, int64_t ccx, int64_t ccy) {
    // weights: iron 0.40, copper 0.30, coal 0.10, limestone 0.20
    static const std::pair<uint8_t, double> weights[] = {{1, 0.40}, {2, 0.30}, {3, 0.10}, {4, 0.20}};
    double roll = hash01(seed, ccx, ccy, SALT_TYPE);
    double cumulative = 0.0;
    for (auto& w : weights) {
        cumulative += w.second;
        if (roll < cumulative) return w.first;
    }
    return 4;
}

} // namespace

ItemId oreForTileType(uint8_t t) {
    static ItemId ids[5] = {NO_ITEM, itemByStringId("iron_ore"), itemByStringId("copper_ore"),
                            itemByStringId("coal"), itemByStringId("limestone")};
    return t < 5 ? ids[t] : NO_ITEM;
}

uint8_t tileTypeAt(int64_t seed, int tileX, int tileY) {
    int64_t ccx0 = floorDiv(tileX, CANDIDATE_CELL_TILES);
    int64_t ccy0 = floorDiv(tileY, CANDIDATE_CELL_TILES);
    const double maxReach = PATCH_BASE_RADIUS_TILES + PATCH_RADIUS_JITTER_TILES + PATCH_EDGE_NOISE_STRENGTH;
    const int reach = int(maxReach / CANDIDATE_CELL_TILES) + 2;

    double wobble = (valueNoise(seed, tileX, tileY, SALT_EDGE, PATCH_EDGE_NOISE_SCALE) - 0.5) * 2 * PATCH_EDGE_NOISE_STRENGTH;

    uint8_t bestType = EMPTY_TILE;
    double bestDist = -1;
    for (int dcy = -reach; dcy <= reach; ++dcy) {
        for (int dcx = -reach; dcx <= reach; ++dcx) {
            int64_t ccx = ccx0 + dcx, ccy = ccy0 + dcy;
            if (!isPatchCenter(seed, ccx, ccy)) continue;
            double cx, cy;
            candidatePosition(seed, ccx, ccy, cx, cy);
            double dist = std::hypot(double(tileX) - cx, double(tileY) - cy);
            double radius = patchRadius(seed, ccx, ccy) + wobble;
            if (dist <= radius && (bestDist < 0 || dist < bestDist)) {
                bestDist = dist;
                bestType = patchTileType(seed, ccx, ccy);
            }
        }
    }
    return bestType;
}

void generateChunk(int64_t seed, int cx, int cy, uint8_t* out) {
    int bx = cx * CHUNK_SIZE, by = cy * CHUNK_SIZE;
    for (int ly = 0; ly < CHUNK_SIZE; ++ly)
        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
            out[ly * CHUNK_SIZE + lx] = tileTypeAt(seed, bx + lx, by + ly);
}

void clearCache() { centerCache().clear(); }

} // namespace terrain
