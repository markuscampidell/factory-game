// core/ItemFilter.hpp - a togglable whitelist with a fixed number of slots.
// Used once per belt tile and once per splitter output side.
// Disabled -> everything passes. Enabled -> only items in a slot pass (an
// enabled filter with every slot empty blocks everything).
#pragma once
#include "Items.hpp"
#include <array>
#include <algorithm>

struct ItemFilter {
    static constexpr int SLOT_COUNT = 10;
    bool enabled = false;
    std::array<ItemId, SLOT_COUNT> slots{};

    bool accepts(ItemId item) const {
        if (!enabled) return true;
        return std::find(slots.begin(), slots.end(), item) != slots.end();
    }
    bool contains(ItemId item) const {
        return std::find(slots.begin(), slots.end(), item) != slots.end();
    }
};
