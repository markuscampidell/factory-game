// game/Assets.cpp
#include "Assets.hpp"
#include <map>
#include <vector>

namespace assets {
namespace {

std::vector<Texture2D> g_items;           // index = ItemId, id 0 unused
std::vector<bool> g_itemLoaded;
Texture2D g_machines[int(MachineType::Count)];
bool g_machineLoaded[int(MachineType::Count)] = {};
Texture2D g_straight[4], g_curve[4][4], g_inter[4][4], g_opposite[4], g_cross[4];
std::map<std::pair<int, bool>, Font> g_fonts;
std::string g_fontPath, g_boldPath;

bool tryLoad(const std::string& path, Texture2D& out) {
    if (path.empty() || !FileExists(path.c_str())) return false;
    out = LoadTexture(path.c_str());
    return out.id != 0;
}

Texture2D loadOrPlaceholder(const std::string& path) {
    Texture2D t{};
    if (tryLoad(path, t)) return t;
    Image img = GenImageChecked(32, 32, 8, 8, MAGENTA, BLACK);
    t = LoadTextureFromImage(img);
    UnloadImage(img);
    return t;
}

std::string pickFont(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates)
        if (FileExists(c)) return c;
    return "";
}

} // namespace

void load() {
    const auto& items = allItems();
    g_items.assign(items.size(), Texture2D{});
    g_itemLoaded.assign(items.size(), false);
    for (size_t i = 1; i < items.size(); ++i) g_itemLoaded[i] = tryLoad(items[i].spritePath, g_items[i]);

    for (int t = 0; t < int(MachineType::Count); ++t)
        g_machineLoaded[t] = tryLoad(machineSpec(MachineType(t)).sprite, g_machines[t]);

    const std::string c = "assets/sprites/conveyors/";
    for (int o = 0; o < 4; ++o) {
        Dir out = Dir(o);
        g_straight[o] = loadOrPlaceholder(c + "straight/belt_straight_" + dirName(out) + ".png");
    }
    // curves: incoming travel direction -> outgoing direction
    for (int o = 0; o < 4; ++o)
        for (int i = 0; i < 4; ++i) {
            Dir in = Dir(i), out = Dir(o);
            if (in == out || in == opposite(out)) continue;
            g_curve[i][o] = loadOrPlaceholder(c + "curve/belt_curve_" + dirName(in) + "_to_" + dirName(out) + ".png");
        }
    // intersections (straight + one side), named as in the Python table
    struct IN { Dir side; Dir out; const char* file; };
    const IN inter[] = {
        {Dir::Up, Dir::Right, "right_up_to_right"},   {Dir::Down, Dir::Right, "down_right_to_right"},
        {Dir::Up, Dir::Left, "left_up_to_left"},      {Dir::Down, Dir::Left, "down_left_to_left"},
        {Dir::Right, Dir::Up, "right_up_to_up"},      {Dir::Left, Dir::Up, "left_up_to_up"},
        {Dir::Right, Dir::Down, "down_right_to_down"}, {Dir::Left, Dir::Down, "down_left_to_down"},
    };
    for (auto& e : inter) g_inter[int(e.side)][int(e.out)] = loadOrPlaceholder(c + "intersection/" + e.file + ".png");
    g_opposite[int(Dir::Right)] = loadOrPlaceholder(c + "opposite/down_up_to_right.png");
    g_opposite[int(Dir::Left)] = loadOrPlaceholder(c + "opposite/down_up_to_left.png");
    g_opposite[int(Dir::Up)] = loadOrPlaceholder(c + "opposite/right_left_to_up.png");
    g_opposite[int(Dir::Down)] = loadOrPlaceholder(c + "opposite/right_left_to_down.png");
    g_cross[int(Dir::Right)] = loadOrPlaceholder(c + "crosssection/down_right_up_to_right.png");
    g_cross[int(Dir::Left)] = loadOrPlaceholder(c + "crosssection/down_left_up_to_left.png");
    g_cross[int(Dir::Up)] = loadOrPlaceholder(c + "crosssection/right_left_up_to_up.png");
    g_cross[int(Dir::Down)] = loadOrPlaceholder(c + "crosssection/down_right_left_to_down.png");

    // Arial on Windows; otherwise (browser, Linux) the bundled Liberation Sans,
    // which has exactly Arial's letter widths, so the layout is the same.
    g_fontPath = pickFont({"C:/Windows/Fonts/arial.ttf", "assets/fonts/LiberationSans-Regular.ttf",
                           "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"});
    g_boldPath = pickFont({"C:/Windows/Fonts/arialbd.ttf", "assets/fonts/LiberationSans-Bold.ttf",
                           "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"});
    if (g_boldPath.empty()) g_boldPath = g_fontPath;
}

void unload() {
    for (size_t i = 1; i < g_items.size(); ++i) if (g_itemLoaded[i]) UnloadTexture(g_items[i]);
    for (int t = 0; t < int(MachineType::Count); ++t) if (g_machineLoaded[t]) UnloadTexture(g_machines[t]);
    for (auto& kv : g_fonts)
        if (kv.second.texture.id != GetFontDefault().texture.id) UnloadFont(kv.second);
    g_fonts.clear();
}

const Texture2D* item(ItemId id) {
    if (id == NO_ITEM || id >= g_items.size() || !g_itemLoaded[id]) return nullptr;
    return &g_items[id];
}

const Texture2D* machine(MachineType t) {
    return g_machineLoaded[int(t)] ? &g_machines[int(t)] : nullptr;
}

const Texture2D& belt(uint8_t mask, Dir out) {
    if (mask == 0) mask = dirBit(out);
    Dir first = rotateCW(out, -1);   // perpendiculars
    Dir second = rotateCW(out, 1);
    // Python lists the perpendiculars as (up, down) / (right, left); which
    // one is "first" doesn't matter for the result.
    bool straight = mask & dirBit(out);
    bool a = mask & dirBit(first);
    bool b = mask & dirBit(second);
    int o = int(out);
    if (straight && a && b) return g_cross[o];
    if (straight && a) return g_inter[int(first)][o];
    if (straight && b) return g_inter[int(second)][o];
    if (a && b) return g_opposite[o];
    if (a) return g_curve[int(first)][o];
    if (b) return g_curve[int(second)][o];
    return g_straight[o];
}

const Font& font(int size, bool bold) {
    auto key = std::make_pair(size, bold);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font f;
    const std::string& path = bold ? g_boldPath : g_fontPath;
    if (!path.empty()) {
        f = LoadFontEx(path.c_str(), size, nullptr, 0);
        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        f = GetFontDefault();
    }
    return g_fonts.emplace(key, f).first->second;
}

} // namespace assets
