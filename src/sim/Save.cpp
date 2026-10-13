// sim/Save.cpp
#include "Save.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace save {

#ifdef __EMSCRIPTEN__
void initStorage() {
    EM_ASM({
        Module.savesReady = 0;
        FS.mkdir('/saves');
        FS.mount(IDBFS, {}, '/saves');
        FS.syncfs(true, function(err) { Module.savesReady = 1; });
    });
}
bool storageReady() { return EM_ASM_INT({ return Module.savesReady ? 1 : 0; }) != 0; }
// copy the save folder back into the browser's storage
static void persist() { EM_ASM({ FS.syncfs(false, function(err) {}); }); }
#else
void initStorage() {}
bool storageReady() { return true; }
static void persist() {}
#endif

namespace {

#ifdef __EMSCRIPTEN__
std::string g_dir = "/saves";
#else
std::string g_dir = "saves";
#endif
constexpr int SAVE_VERSION = 1;

fs::path dirPath() {
    fs::path p = fs::u8path(g_dir);
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

fs::path pathFor(const std::string& name) { return dirPath() / fs::u8path(sanitizeName(name) + ".json"); }

// ---- inventories ----
json slotsToJson(const Inventory& inv) {
    json rows = json::array();
    for (int y = 0; y < inv.height(); ++y) {
        json row = json::array();
        for (int x = 0; x < inv.width(); ++x) {
            const Slot& s = inv.at(x, y);
            if (s.empty()) row.push_back(nullptr);
            else row.push_back({{"item", itemDef(s.item).id}, {"amount", s.amount}});
        }
        rows.push_back(row);
    }
    return rows;
}

// Reads Python-style [[{item, amount} | null, ...], ...] into `inv`.
// Unknown items or slots that don't fit are added wherever there is room;
// anything left over is reported through `overflow` instead of vanishing.
void slotsFromJson(const json& j, Inventory& inv, ItemAmounts* overflow) {
    inv.clear();
    if (!j.is_array()) return;
    ItemAmounts extra;
    for (int y = 0; y < int(j.size()); ++y) {
        const json& row = j[size_t(y)];
        if (!row.is_array()) continue;
        for (int x = 0; x < int(row.size()); ++x) {
            const json& s = row[size_t(x)];
            if (!s.is_object()) continue;
            ItemId id = itemByStringId(s.value("item", std::string()));
            int amount = s.value("amount", 0);
            if (id == NO_ITEM || amount <= 0) continue;
            if (x < inv.width() && y < inv.height() && inv.at(x, y).empty() && amount <= Inventory::MAX_STACK_SIZE)
                inv.at(x, y) = Slot{id, amount};
            else
                addAmount(extra, id, amount);
        }
    }
    for (auto& p : extra) {
        int left = p.second;
        while (left > 0 && inv.canAdd(p.first, 1)) {
            int chunk = std::min(left, Inventory::MAX_STACK_SIZE);
            while (chunk > 0 && !inv.canAdd(p.first, chunk)) --chunk;
            inv.tryAdd(p.first, chunk);
            left -= chunk;
        }
        if (left > 0 && overflow) addAmount(*overflow, p.first, left);
    }
}

ItemAmounts slotsTotal(const json& j) {
    ItemAmounts out;
    if (!j.is_array()) return out;
    for (auto& row : j) {
        if (!row.is_array()) continue;
        for (auto& s : row)
            if (s.is_object()) {
                ItemId id = itemByStringId(s.value("item", std::string()));
                int amount = s.value("amount", 0);
                if (id != NO_ITEM && amount > 0) addAmount(out, id, amount);
            }
    }
    return out;
}

ItemFilter filterFromJson(const json& j) {
    ItemFilter f;
    if (!j.is_object()) return f;
    f.enabled = j.value("enabled", false);
    if (j.contains("slots") && j["slots"].is_array()) {
        const json& s = j["slots"];
        for (size_t i = 0; i < s.size() && i < size_t(ItemFilter::SLOT_COUNT); ++i)
            if (s[i].is_string()) f.slots[i] = itemByStringId(s[i].get<std::string>());
    } else if (j.contains("allowed_items") && j["allowed_items"].is_array()) {
        size_t i = 0;
        for (auto& it : j["allowed_items"])
            if (it.is_string() && i < size_t(ItemFilter::SLOT_COUNT)) f.slots[i++] = itemByStringId(it.get<std::string>());
    }
    return f;
}

json filterToJson(const ItemFilter& f) {
    json slots = json::array();
    for (ItemId id : f.slots) {
        if (id == NO_ITEM) slots.push_back(nullptr);
        else slots.push_back(itemDef(id).id);
    }
    return {{"enabled", f.enabled}, {"slots", slots}};
}

bool dirFromJson(const json& j, Dir& out) {
    if (!j.is_array() || j.size() != 2) return false;
    Vec2i v{int(std::lround(j[0].get<double>())), int(std::lround(j[1].get<double>()))};
    return dirFromVec(v, out);
}

json dirToJson(Dir d) { Vec2i v = dirVec(d); return json::array({v.x, v.y}); }

Vec2i posFromJson(const json& j) {
    return {int(std::lround(j[0].get<double>())), int(std::lround(j[1].get<double>()))};
}

json machineToJson(const Machine& m) {
    json j;
    j["type"] = machineSpec(m.type).saveType;
    j["grid_pos"] = json::array({m.pos.x, m.pos.y});
    if (m.isProducing()) {
        j["recipe_id"] = m.recipe ? json(m.recipe->id) : json(nullptr);
        json in = json::object(), out = json::object();
        for (auto& b : m.inputs) in[itemDef(b.item).id] = slotsToJson(b.inv);
        for (auto& b : m.outputs) out[itemDef(b.item).id] = slotsToJson(b.inv);
        j["input_inventories"] = in;
        j["output_inventories"] = out;
        j["processing"] = m.processing;          // C++ extra
        j["process_ticks"] = m.timer;            // C++ extra
        if (m.type == MachineType::Miner) j["ore_item_id"] = itemDef(m.oreItem).id;
        if (machineTurnable(m.type)) j["direction"] = dirToJson(m.dir);   // C++ extra (miner lying on its side)
    } else if (m.isStorage()) {
        j["slots"] = slotsToJson(m.storage);
    } else if (m.isSplitter()) {
        j["direction"] = dirToJson(m.dir);
        j["current_item"] = m.item ? json(itemDef(m.item).id) : json(nullptr);
        json filters = json::array();
        for (auto& f : m.sideFilters) filters.push_back(filterToJson(f));
        j["output_filters"] = filters;
        j["progress"] = m.progress;              // C++ extra
        j["output_index"] = m.outputIndex;       // C++ extra
    }
    return j;
}

// Builds a machine from JSON. Anything it held that can't be placed back
// into it (e.g. its recipe no longer exists) is added to `lost` so the
// caller can hand it to the player instead of deleting it.
std::unique_ptr<Machine> machineFromJson(const json& j, ItemAmounts& lost) {
    MachineType type;
    if (!machineTypeFromSave(j.value("type", std::string()), type)) return nullptr;
    Vec2i pos = posFromJson(j["grid_pos"]);
    std::unique_ptr<Machine> m;
    if (type == MachineType::Miner) {
        ItemId ore = itemByStringId(j.value("ore_item_id", std::string()));
        if (ore == NO_ITEM) ore = itemByStringId("iron_ore");
        Dir d = Dir::Right;
        if (j.contains("direction")) dirFromJson(j["direction"], d);
        m = makeMachine(type, pos, d, ore);
    } else if (type == MachineType::Splitter) {
        Dir d = Dir::Right;
        if (j.contains("direction")) dirFromJson(j["direction"], d);
        m = makeMachine(type, pos, d);
    } else {
        m = makeMachine(type, pos);
    }

    if (m->isProducing()) {
        if (type != MachineType::Miner) {
            const Recipe* r = nullptr;
            if (j.contains("recipe_id") && j["recipe_id"].is_string()) r = findRecipe(j["recipe_id"].get<std::string>());
            m->recipe = r;   // may be null if the recipe was removed
            m->resetBuffers();
        }
        auto loadSide = [&](const char* key, bool isInput) {
            if (!j.contains(key) || !j[key].is_object()) return;
            for (auto it = j[key].begin(); it != j[key].end(); ++it) {
                ItemId id = itemByStringId(it.key());
                MachineBuffer* buf = id ? (isInput ? m->inputBuffer(id) : m->outputBuffer(id)) : nullptr;
                if (buf) {
                    ItemAmounts over;
                    slotsFromJson(it.value(), buf->inv, &over);
                    for (auto& p : over) addAmount(lost, p.first, p.second);
                } else {
                    for (auto& p : slotsTotal(it.value())) addAmount(lost, p.first, p.second);
                }
            }
        };
        loadSide("input_inventories", true);
        loadSide("output_inventories", false);
        m->processing = j.value("processing", false);
        m->timer = j.value("process_ticks", 0);
        if (!m->recipe) { m->processing = false; m->timer = 0; }
    } else if (m->isStorage()) {
        ItemAmounts over;
        if (j.contains("slots")) slotsFromJson(j["slots"], m->storage, &over);
        for (auto& p : over) addAmount(lost, p.first, p.second);
    } else if (m->isSplitter()) {
        if (j.contains("current_item") && j["current_item"].is_string())
            m->item = itemByStringId(j["current_item"].get<std::string>());
        if (j.contains("output_filters") && j["output_filters"].is_array())
            for (size_t i = 0; i < 3 && i < j["output_filters"].size(); ++i)
                m->sideFilters[i] = filterFromJson(j["output_filters"][i]);
        m->progress = uint8_t(std::clamp(j.value("progress", 0), 0, BELT_TICKS_PER_TILE));
        m->outputIndex = uint8_t(std::clamp(j.value("output_index", 0), 0, 2));
    }
    return m;
}

int64_t randomSeed() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    return int64_t(gen() % 2147483647ULL);
}

} // namespace

void setSavesDir(const std::string& dir) { g_dir = dir; }
const std::string& savesDir() { return g_dir; }

std::string sanitizeName(const std::string& raw) {
    // strip surrounding whitespace
    size_t a = raw.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = raw.find_last_not_of(" \t\r\n");
    std::string name = raw.substr(a, b - a + 1);
    for (char& c : name) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
        if (!ok) c = '_';
    }
    // strip " ._" from both ends (Python: name.strip(" ._"))
    auto strip = [](char c) { return c == ' ' || c == '.' || c == '_'; };
    size_t s = 0, e = name.size();
    while (s < e && strip(name[s])) ++s;
    while (e > s && strip(name[e - 1])) --e;
    name = name.substr(s, e - s);
    if (name.size() > 64) name.resize(64);
    return name;
}

bool isValidName(const std::string& name) {
    size_t a = name.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    size_t b = name.find_last_not_of(" \t\r\n");
    std::string stripped = name.substr(a, b - a + 1);
    std::string clean = sanitizeName(name);
    return !clean.empty() && clean == stripped;
}

bool exists(const std::string& name) {
    if (sanitizeName(name).empty()) return false;
    std::error_code ec;
    return fs::exists(pathFor(name), ec);
}

std::vector<std::string> listSaves() {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dirPath(), ec)) {
        if (e.path().extension() == ".json") out.push_back(e.path().stem().u8string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

double readPlayTime(const std::string& name) {
    std::ifstream f(pathFor(name));
    if (!f) return -1;
    try {
        json data;
        f >> data;
        return std::max(0.0, data.value("play_time", 0.0));
    } catch (const std::exception&) {
        return -1;
    }
}

void remove(const std::string& name) {
    std::error_code ec;
    fs::remove(pathFor(name), ec);
    persist();
}

bool rename(const std::string& oldName, const std::string& newName) {
    bool wasLast = lastOpened() == oldName;
    std::error_code ec;
    fs::rename(pathFor(oldName), pathFor(newName), ec);
    if (ec) return false;
    if (wasLast) setLastOpened(newName);
    persist();
    return true;
}

std::string lastOpened() {
    std::ifstream f(dirPath() / ".last_opened");
    if (!f) return "";
    std::string name((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t a = name.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    name = name.substr(a, name.find_last_not_of(" \t\r\n") - a + 1);
    return exists(name) ? name : "";
}

void setLastOpened(const std::string& name) {
    {
        std::ofstream f(dirPath() / ".last_opened", std::ios::trunc);
        f << name;
    }
    persist();
}

bool saveGame(const World& w, const Player& p, const CameraState& cam, const std::string& name, std::string* error) {
    json data;
    data["version"] = SAVE_VERSION;
    data["player"] = {{"x", p.rect.cx()}, {"y", p.rect.cy()}, {"inventory", slotsToJson(p.inventory)}};
    data["camera"] = {{"x", cam.x}, {"y", cam.y}, {"zoom", cam.zoom}, {"show_outputs", cam.showOutputs}};
    json markers = json::array();
    for (const MapMarker& m : cam.markers) markers.push_back({{"x", m.tile.x}, {"y", m.tile.y}, {"name", m.name}, {"symbol", m.symbol}});
    data["markers"] = markers;
    data["play_time"] = cam.playSeconds;
    data["seed"] = w.seed;
    json belts = json::array();
    for (const Belt& b : w.belts()) {
        if (!b.alive) continue;
        json jb;
        jb["grid_pos"] = json::array({b.pos.x, b.pos.y});
        jb["direction"] = dirToJson(b.dir);
        jb["item"] = b.item ? json(itemDef(b.item).id) : json(nullptr);
        jb["filter"] = filterToJson(b.filter);
        jb["progress"] = b.progress;               // C++ extra
        jb["item_from"] = dirToJson(b.itemFrom);   // C++ extra
        belts.push_back(jb);
    }
    data["belts"] = belts;
    json machines = json::array();
    for (size_t i = 0; i < w.machineSlots(); ++i)
        if (w.machineAlive(int(i))) machines.push_back(machineToJson(w.machine(int(i))));
    data["machines"] = machines;
    json explored = json::array();   // C++ extra: chunks the player has seen
    for (Vec2i c : w.explored()) explored.push_back(json::array({c.x, c.y}));
    data["explored_chunks"] = explored;

    fs::path path = pathFor(name);
    fs::path tmp = path;
    tmp.replace_extension(".tmp");
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) { if (error) *error = "Could not write " + tmp.u8string(); return false; }
        f << data.dump(2);
        if (!f) { if (error) *error = "Could not write " + tmp.u8string(); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Windows can refuse to rename over an existing file
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) { if (error) *error = ec.message(); return false; }
    }
    persist();
    return true;
}

bool loadGame(World& w, Player& p, CameraState& cam, const std::string& name, std::string* error) {
    std::ifstream f(pathFor(name));
    if (!f) { if (error) *error = "Save not found"; return false; }
    json data;
    try { f >> data; } catch (const std::exception& e) {
        if (error) *error = std::string("Broken save file: ") + e.what();
        return false;
    }

    try {
        w.clear();
        w.seed = data.contains("seed") ? data["seed"].get<int64_t>() : randomSeed();

        p = Player{};
        const json& jp = data["player"];
        p.setCenter(jp.value("x", 0.0f), jp.value("y", 0.0f));
        ItemAmounts lost;
        slotsFromJson(jp["inventory"], p.inventory, &lost);

        cam = {};
        if (data.contains("camera") && data["camera"].is_object()) {
            cam.x = data["camera"].value("x", 0.0f);
            cam.y = data["camera"].value("y", 0.0f);
            cam.zoom = data["camera"].value("zoom", 1.0f);
            cam.showOutputs = data["camera"].value("show_outputs", true);
            cam.valid = true;
        }
        cam.playSeconds = std::max(0.0, data.value("play_time", 0.0));
        for (const json& jm : data.value("markers", json::array()))
            if (jm.is_object())
                cam.markers.push_back({{jm.value("x", 0), jm.value("y", 0)}, jm.value("name", std::string("Marker")), jm.value("symbol", std::string())});

        if (data.contains("explored_chunks") && data["explored_chunks"].is_array())
            for (const json& c : data["explored_chunks"])
                if (c.is_array() && c.size() == 2) w.markExplored(c[0].get<int>(), c[1].get<int>());

        for (const json& jb : data["belts"]) {
            Vec2i pos = posFromJson(jb["grid_pos"]);
            Dir d = Dir::Right;
            dirFromJson(jb["direction"], d);
            if (w.isCellBlocked(pos)) {
                if (jb.contains("item") && jb["item"].is_string()) addAmount(lost, itemByStringId(jb["item"].get<std::string>()), 1);
                continue;
            }
            int id = w.addBelt(pos, d);
            Belt& b = w.belt(id);
            if (jb.contains("item") && jb["item"].is_string()) b.item = itemByStringId(jb["item"].get<std::string>());
            if (jb.contains("filter")) b.filter = filterFromJson(jb["filter"]);
            else b.filter = filterFromJson({{"enabled", jb.value("filter_enabled", false)},
                                            {"allowed_items", jb.value("allowed_items", json::array())}});
            b.progress = uint8_t(std::clamp(jb.value("progress", 0), 0, BELT_TICKS_PER_TILE));
            if (jb.contains("item_from")) dirFromJson(jb["item_from"], b.itemFrom);
            else b.itemFrom = d;
        }

        for (const json& jm : data["machines"]) {
            auto m = machineFromJson(jm, lost);
            if (!m) continue;
            bool free = true;
            for (int dy = 0; dy < m->h && free; ++dy)
                for (int dx = 0; dx < m->w && free; ++dx)
                    if (w.isCellBlocked({m->pos.x + dx, m->pos.y + dy})) free = false;
            if (!free) {
                for (auto& it : m->contents()) addAmount(lost, it.first, it.second);
                for (auto& it : machineSpec(m->type).cost) addAmount(lost, it.first, it.second);
                continue;
            }
            w.addMachine(std::move(m));
        }

        // Anything that had nowhere to go goes into the player's inventory
        // (or, if that's full, a storage box placed next to the player), so
        // loading never deletes items.
        if (!lost.empty()) {
            ItemAmounts stillLost;
            for (auto& it : lost) {
                if (it.first == NO_ITEM) continue;
                int left = it.second;
                while (left > 0) {
                    int n = std::min(left, Inventory::MAX_STACK_SIZE);
                    while (n > 0 && !p.inventory.canAdd(it.first, n)) --n;
                    if (n == 0) break;
                    p.inventory.tryAdd(it.first, n);
                    left -= n;
                }
                if (left > 0) addAmount(stillLost, it.first, left);
            }
            int guard = 0;
            int ring = 2;
            while (!stillLost.empty() && guard < 400) {
                Vec2i base{int(std::floor(p.rect.cx() / CELL_SIZE)) + ring, int(std::floor(p.rect.cy() / CELL_SIZE)) + (guard % 20) * 2 - 20};
                ++guard;
                if (guard % 20 == 0) ring += 3;
                bool free = true;
                for (int dy = 0; dy < 2 && free; ++dy)
                    for (int dx = 0; dx < 2 && free; ++dx)
                        if (w.isCellBlocked({base.x + dx, base.y + dy})) free = false;
                if (!free) continue;
                auto box = makeMachine(MachineType::Storage, base);
                ItemAmounts rest;
                for (auto& it : stillLost) {
                    int left = it.second;
                    while (left > 0) {
                        int n = std::min(left, Inventory::MAX_STACK_SIZE);
                        while (n > 0 && !box->storage.canAdd(it.first, n)) --n;
                        if (n == 0) break;
                        box->storage.tryAdd(it.first, n);
                        left -= n;
                    }
                    if (left > 0) addAmount(rest, it.first, left);
                }
                w.addMachine(std::move(box));
                stillLost = rest;
            }
        }
    } catch (const std::exception& e) {
        if (error) *error = std::string("Broken save file: ") + e.what();
        return false;
    }

    w.ensureTopology();
    setLastOpened(name);
    return true;
}

void newGame(World& w, Player& p, CameraState& cam, const std::string& name) {
    w.clear();
    w.seed = randomSeed();
    p = Player{};
    cam = {};
    saveGame(w, p, cam, name);
    setLastOpened(name);
}

// ---------------------------------------------------------------------------
// Blueprints
// ---------------------------------------------------------------------------
static fs::path blueprintsPath() { return dirPath() / "blueprints.bpjson"; }

std::vector<Blueprint> loadBlueprints(std::vector<BlueprintBook>* booksOut) {
    std::vector<Blueprint> out;
    std::vector<BlueprintBook> books;
    std::ifstream f(blueprintsPath());
    if (!f) return out;
    try {
        json data;
        f >> data;
        for (const json& jk : data.value("books", json::array())) {
            BlueprintBook bk;
            bk.id = jk.value("id", 0);
            bk.name = jk.value("name", std::string("Book"));
            bool dup = false;
            for (auto& o : books) dup |= o.id == bk.id;
            if (bk.id > 0 && !dup) books.push_back(bk);
        }
        for (const json& jb : data.value("blueprints", json::array())) {
            Blueprint bp;
            bp.name = jb.value("name", std::string("Blueprint"));
            bp.book = jb.value("book", 0);
            bool found = false;
            for (auto& bk : books) found |= bk.id == bp.book;
            if (!found) bp.book = 0;   // its book is gone: outside
            bp.w = jb.value("w", 0);
            bp.h = jb.value("h", 0);
            for (const json& b : jb.value("belts", json::array())) {
                BlueprintBelt bb;
                bb.off = {b.value("x", 0), b.value("y", 0)};
                if (b.contains("direction")) dirFromJson(b["direction"], bb.dir);
                if (b.contains("filter")) bb.filter = filterFromJson(b["filter"]);
                bp.belts.push_back(bb);
            }
            for (const json& m : jb.value("machines", json::array())) {
                BlueprintMachine bm;
                if (!machineTypeFromSave(m.value("type", std::string()), bm.type)) continue;
                bm.off = {m.value("x", 0), m.value("y", 0)};
                if (m.contains("direction")) dirFromJson(m["direction"], bm.dir);
                bm.recipeId = m.value("recipe_id", std::string());
                if (m.contains("output_filters") && m["output_filters"].is_array())
                    for (size_t k = 0; k < 3 && k < m["output_filters"].size(); ++k)
                        bm.sideFilters[k] = filterFromJson(m["output_filters"][k]);
                bp.machines.push_back(bm);
            }
            bp.computeMasks();
            if (!bp.empty()) out.push_back(bp);
        }
    } catch (const std::exception&) {
        // a broken blueprint file just means no blueprints
    }
    if (booksOut) *booksOut = books;
    return out;
}

bool saveBlueprints(const std::vector<Blueprint>& list, const std::vector<BlueprintBook>& books) {
    json bookArr = json::array();
    for (const BlueprintBook& bk : books) bookArr.push_back({{"id", bk.id}, {"name", bk.name}});
    json arr = json::array();
    for (const Blueprint& bp : list) {
        json jb;
        jb["name"] = bp.name;
        if (bp.book > 0) jb["book"] = bp.book;
        jb["w"] = bp.w;
        jb["h"] = bp.h;
        json belts = json::array();
        for (auto& b : bp.belts)
            belts.push_back({{"x", b.off.x}, {"y", b.off.y}, {"direction", dirToJson(b.dir)}, {"filter", filterToJson(b.filter)}});
        json machines = json::array();
        for (auto& m : bp.machines) {
            json filters = json::array();
            for (auto& f : m.sideFilters) filters.push_back(filterToJson(f));
            machines.push_back({{"type", machineSpec(m.type).saveType}, {"x", m.off.x}, {"y", m.off.y},
                                {"direction", dirToJson(m.dir)}, {"recipe_id", m.recipeId}, {"output_filters", filters}});
        }
        jb["belts"] = belts;
        jb["machines"] = machines;
        arr.push_back(jb);
    }
    fs::path path = blueprintsPath(), tmp = path;
    tmp.replace_extension(".tmp");
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << json{{"version", 2}, {"books", bookArr}, {"blueprints", arr}}.dump(2);
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(path, ec); fs::rename(tmp, path, ec); }
    persist();
    return !ec;
}

} // namespace save
