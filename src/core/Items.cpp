// core/Items.cpp
#include "Items.hpp"
#include "Types.hpp"

namespace {

std::vector<ItemDef> makeItems() {
    const std::string base = "assets/sprites/items/";
    return {
        {"", "Nothing", ""},
        // raw materials
        {"limestone", "Limestone", ""},
        {"iron_ore", "Iron Ore", base + "raw_materials/iron_ore.png"},
        {"copper_ore", "Copper Ore", base + "raw_materials/copper_ore.png"},
        {"coal", "Coal", base + "raw_materials/coal.png"},
        // ingots
        {"iron_ingot", "Iron Ingot", base + "ingots/iron_ingot.png"},
        {"copper_ingot", "Copper Ingot", base + "ingots/copper_ingot.png"},
        {"steel", "Steel", base + "ingots/steel.png"},
        // minerals
        {"concrete", "Concrete", base + "minerals/concrete.png"},
        // standard parts
        {"iron_plate", "Iron Plate", base + "standard_parts/iron_plate.png"},
        {"iron_rod", "Iron Rod", base + "standard_parts/iron_rod.png"},
    };
}

ItemId I(const char* id) { return itemByStringId(id); }

Recipe make(const char* id, const char* name,
            std::initializer_list<std::pair<const char*, int>> in,
            std::initializer_list<std::pair<const char*, int>> out,
            double seconds) {
    Recipe r;
    r.id = id;
    r.name = name;
    for (auto& p : in) r.inputs.push_back({I(p.first), p.second});
    for (auto& p : out) r.outputs.push_back({I(p.first), p.second});
    r.ticks = int(seconds * TICKS_PER_SECOND + 0.5);
    if (r.ticks < 1) r.ticks = 1;
    return r;
}

struct RecipeBook {
    std::vector<Recipe> storage;
    std::vector<const Recipe*> smelter, assembler, all;
    RecipeBook() {
        storage.reserve(16);
        // Smelter
        storage.push_back(make("iron_ingot", "Iron Ingot", {{"iron_ore", 3}}, {{"iron_ingot", 1}}, 2));
        storage.push_back(make("copper_ingot", "Copper Ingot", {{"copper_ore", 3}}, {{"copper_ingot", 1}}, 2));
        storage.push_back(make("steel", "Steel", {{"iron_ingot", 2}, {"coal", 2}}, {{"steel", 1}}, 3));
        // Assembler
        storage.push_back(make("iron_plate", "Iron Plate", {{"iron_ingot", 3}}, {{"iron_plate", 2}}, 6));
        storage.push_back(make("iron_rod", "Iron Rod", {{"iron_ingot", 1}}, {{"iron_rod", 5}}, 1));

        for (int i = 0; i < 3; ++i) smelter.push_back(&storage[i]);
        for (int i = 3; i < 5; ++i) assembler.push_back(&storage[i]);
        all = smelter;
        all.insert(all.end(), assembler.begin(), assembler.end());
    }
};

const RecipeBook& book() {
    static RecipeBook b;
    return b;
}

} // namespace

const std::vector<ItemDef>& allItems() {
    static std::vector<ItemDef> items = makeItems();
    return items;
}

int itemCount() { return int(allItems().size()) - 1; }

ItemId itemByStringId(const std::string& id) {
    const auto& items = allItems();
    for (size_t i = 1; i < items.size(); ++i)
        if (items[i].id == id) return ItemId(i);
    return NO_ITEM;
}

void addAmount(ItemAmounts& list, ItemId item, int amount) {
    for (auto& p : list)
        if (p.first == item) { p.second += amount; return; }
    list.push_back({item, amount});
}

const std::vector<const Recipe*>& smelterRecipes() { return book().smelter; }
const std::vector<const Recipe*>& assemblerRecipes() { return book().assembler; }
const std::vector<const Recipe*>& handcraftRecipes() { return book().all; }

const Recipe* findRecipe(const std::string& id) {
    for (auto* r : book().all)
        if (r->id == id) return r;
    return nullptr;
}
