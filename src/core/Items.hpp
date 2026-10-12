// core/Items.hpp - every item type and every recipe in the game.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

// Items are referred to by a small integer everywhere in the simulation
// (fast to copy and compare). 0 means "no item". The string ids
// ("iron_ore", ...) are only used for save files and lookups.
using ItemId = uint16_t;
constexpr ItemId NO_ITEM = 0;

struct ItemDef {
    std::string id;
    std::string name;
    std::string spritePath; // relative to the game folder, may not exist
};

// All item definitions, index == ItemId. Index 0 is a dummy "none" entry.
const std::vector<ItemDef>& allItems();
inline const ItemDef& itemDef(ItemId id) { return allItems()[id]; }
ItemId itemByStringId(const std::string& id);   // NO_ITEM if unknown
int itemCount();                                  // number of real items

using ItemAmounts = std::vector<std::pair<ItemId, int>>;

// Adds `amount` of `item` into an ordered list (keeps first-seen order).
void addAmount(ItemAmounts& list, ItemId item, int amount);

struct Recipe {
    std::string id;
    std::string name;
    ItemAmounts inputs;
    ItemAmounts outputs;
    int ticks = 60;           // processing time in simulation ticks
    double seconds() const { return double(ticks) / 60.0; }
    double perMinute(int amount) const { return amount * 60.0 / seconds(); }
};

const std::vector<const Recipe*>& smelterRecipes();
const std::vector<const Recipe*>& assemblerRecipes();
const std::vector<const Recipe*>& handcraftRecipes(); // smelter + assembler
const Recipe* findRecipe(const std::string& id);      // nullptr if unknown
