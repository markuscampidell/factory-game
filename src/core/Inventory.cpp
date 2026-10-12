// core/Inventory.cpp
#include "Inventory.hpp"
#include <algorithm>

void Inventory::clear() {
    for (auto& s : slots_) s = Slot{};
}

bool Inventory::isEmpty() const {
    for (auto& s : slots_)
        if (!s.empty()) return false;
    return true;
}

int Inventory::amount(ItemId item) const {
    int total = 0;
    for (auto& s : slots_)
        if (s.item == item) total += s.amount;
    return total;
}

bool Inventory::canAdd(ItemId item, int amount) const {
    if (amount <= 0) return true;
    if (item == NO_ITEM) return false;
    long long remaining = amount;
    for (auto& s : slots_) {
        if (s.empty()) remaining -= MAX_STACK_SIZE;
        else if (s.item == item) remaining -= (MAX_STACK_SIZE - s.amount);
        if (remaining <= 0) return true;
    }
    return false;
}

bool Inventory::tryAdd(ItemId item, int amount) {
    if (amount <= 0) return true;
    if (!canAdd(item, amount)) return false;
    int remaining = amount;
    // Top up existing stacks first...
    for (auto& s : slots_) {
        if (!s.empty() && s.item == item && s.amount < MAX_STACK_SIZE) {
            int add = std::min(MAX_STACK_SIZE - s.amount, remaining);
            s.amount += add;
            remaining -= add;
            if (remaining == 0) return true;
        }
    }
    // ...then fill empty slots.
    for (auto& s : slots_) {
        if (s.empty()) {
            int add = std::min(MAX_STACK_SIZE, remaining);
            s.item = item;
            s.amount = add;
            remaining -= add;
            if (remaining == 0) return true;
        }
    }
    return remaining == 0; // unreachable thanks to canAdd()
}

bool Inventory::tryRemove(ItemId item, int amount) {
    if (amount <= 0) return true;
    if (this->amount(item) < amount) return false; // all or nothing
    int remaining = amount;
    bool emptied = false;
    for (auto& s : slots_) {
        if (!s.empty() && s.item == item) {
            int take = std::min(s.amount, remaining);
            s.amount -= take;
            remaining -= take;
            if (s.amount == 0) { s = Slot{}; emptied = true; }
            if (remaining == 0) break;
        }
    }
    tidyAfterRemoval(item, emptied);
    return true;
}

bool Inventory::hasEnough(const ItemAmounts& items) const {
    for (auto& p : items)
        if (amount(p.first) < p.second) return false;
    return true;
}

bool Inventory::tryRemoveAll(const ItemAmounts& items) {
    if (!hasEnough(items)) return false;
    for (auto& p : items) tryRemove(p.first, p.second);
    return true;
}

ItemId Inventory::takeOneFromSlot(int x, int y) {
    Slot& s = at(x, y);
    if (s.empty()) return NO_ITEM;
    ItemId item = s.item;
    s.amount -= 1;
    bool emptied = false;
    if (s.amount == 0) { s = Slot{}; emptied = true; }
    tidyAfterRemoval(item, emptied);
    return item;
}

ItemAmounts Inventory::contents() const {
    ItemAmounts out;
    for (auto& s : slots_)
        if (!s.empty()) addAmount(out, s.item, s.amount);
    return out;
}

bool Inventory::mergeStacks(ItemId item) {
    std::vector<size_t> positions;
    for (size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i].empty() && slots_[i].item == item) positions.push_back(i);
    if (positions.size() < 2) return false;
    int remaining = 0;
    for (auto i : positions) remaining += slots_[i].amount;
    bool emptied = false;
    for (auto i : positions) {
        if (remaining <= 0) { slots_[i] = Slot{}; emptied = true; continue; }
        int amt = std::min(MAX_STACK_SIZE, remaining);
        slots_[i].amount = amt;
        remaining -= amt;
    }
    return emptied;
}

void Inventory::compact() {
    // Same algorithm as the Python version: move the last occupied slot into
    // the first gap, repeatedly.
    if (slots_.empty()) return;
    size_t last = slots_.size() - 1;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].empty()) continue;
        while (last > i && slots_[last].empty()) --last;
        if (last <= i) break;
        slots_[i] = slots_[last];
        slots_[last] = Slot{};
        if (last == 0) break;
        --last;
    }
}

void Inventory::tidyAfterRemoval(ItemId item, bool slotEmptied) {
    if (mergeStacks(item) || slotEmptied) compact();
}

int moveAmount(Inventory& src, Inventory& dest, ItemId item, int amount) {
    if (&src == &dest || item == NO_ITEM) return 0;
    amount = std::min(amount, src.amount(item));
    // Work out how much fits without adding item by item.
    int lo = 0, hi = amount;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (dest.canAdd(item, mid)) lo = mid; else hi = mid - 1;
    }
    if (lo <= 0) return 0;
    // Remove first, then add: both are guaranteed to succeed.
    src.tryRemove(item, lo);
    dest.tryAdd(item, lo);
    return lo;
}

void moveStack(Inventory& src, int x, int y, Inventory& dest, ItemId expected) {
    const Slot& s = src.at(x, y);
    if (s.empty()) return;
    if (expected != NO_ITEM && s.item != expected) return;
    moveAmount(src, dest, s.item, s.amount);
}

void moveAllOfType(Inventory& src, int x, int y, Inventory& dest, ItemId expected) {
    const Slot& s = src.at(x, y);
    if (s.empty()) return;
    if (expected != NO_ITEM && s.item != expected) return;
    ItemId item = s.item;
    moveAmount(src, dest, item, src.amount(item));
}
