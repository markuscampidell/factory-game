// core/Inventory.hpp - a grid of item stacks (player backpack, storage box,
// machine input/output slots).
//
// Every operation here is all-or-nothing: it either does exactly what was
// asked, or changes nothing and returns false. That is what guarantees items
// can never be duplicated or silently lost by the inventory itself.
#pragma once
#include "Items.hpp"
#include <vector>

struct Slot {
    ItemId item = NO_ITEM;
    int amount = 0;
    bool empty() const { return item == NO_ITEM || amount <= 0; }
};

class Inventory {
public:
    static constexpr int MAX_STACK_SIZE = 100;

    Inventory() : Inventory(1, 1) {}
    Inventory(int w, int h) : w_(w), h_(h), slots_(size_t(w * h)) {}

    int width() const { return w_; }
    int height() const { return h_; }
    Slot& at(int x, int y) { return slots_[size_t(y * w_ + x)]; }
    const Slot& at(int x, int y) const { return slots_[size_t(y * w_ + x)]; }
    std::vector<Slot>& slots() { return slots_; }
    const std::vector<Slot>& slots() const { return slots_; }

    void clear();
    bool isEmpty() const;

    int amount(ItemId item) const;
    bool canAdd(ItemId item, int amount) const;
    bool tryAdd(ItemId item, int amount);          // all or nothing
    bool tryRemove(ItemId item, int amount);       // all or nothing
    bool hasEnough(const ItemAmounts& items) const;
    bool tryRemoveAll(const ItemAmounts& items);   // all or nothing

    // Removes exactly one item from slot (x, y) (used when a machine pushes
    // an item onto a belt). Returns the item id, or NO_ITEM.
    ItemId takeOneFromSlot(int x, int y);

    // Totals of every item, in first-seen order.
    ItemAmounts contents() const;

    // Packs stacks of `item` together / closes gaps (same behaviour as the
    // Python version so the grid never shows holes).
    bool mergeStacks(ItemId item);
    void compact();

private:
    void tidyAfterRemoval(ItemId item, bool slotEmptied);
    int w_, h_;
    std::vector<Slot> slots_;
};

// Moves as much of the stack at (x, y) as fits into `dest`.
// expected is a safety check: nothing happens if the slot holds something else.
void moveStack(Inventory& src, int x, int y, Inventory& dest, ItemId expected = NO_ITEM);
// Moves every item of the type at (x, y) (all stacks) as far as it fits.
void moveAllOfType(Inventory& src, int x, int y, Inventory& dest, ItemId expected = NO_ITEM);
// Moves up to `amount` of `item`, stopping when dest is full. Returns moved.
int moveAmount(Inventory& src, Inventory& dest, ItemId item, int amount);
