// game/GamePanels.cpp - inventory, machine, storage, hand-crafting and
// filter panels (port of the ui/ package).
#include "GameScreen.hpp"
#include "Assets.hpp"
#include <algorithm>
#include <cstdio>

namespace {
const Color SLOT_BORDER{170, 170, 170, 255};
const Color SLOT_HOVER{255, 255, 255, 90};

// Item slot frame (+ light highlight under the mouse)
void drawSlotFrame(Rectangle s) {
    if (hovered(s)) DrawRectangleRec({s.x + 2, s.y + 2, s.width - 4, s.height - 4}, SLOT_HOVER);   // inside only
    lineRect(s, 2, SLOT_BORDER);
}
constexpr float INV_SLOT = 48, INV_PAD = 10, INV_TITLE = ui::TITLE_H;

// filter panel geometry
constexpr float F_SLOT = 36, F_GAP = 4, F_CHECK = 20;
constexpr int F_COLS = 5, P_COLS = 4;
constexpr float F_LABEL_H = 24, P_TITLE_H = 21;
constexpr float FP_PAD = 16, FP_TITLE = ui::TITLE_H, FP_PICKER_GAP = 20, FP_COLUMN_GAP = 24;
const char* SIDE_LABELS[3] = {"Left", "Forward", "Right"};

float gridW(int cols) { return cols * (F_SLOT + F_GAP) - F_GAP; }
float gridH(int count, int cols) { int rows = (count + cols - 1) / cols; return rows * (F_SLOT + F_GAP) - F_GAP; }
Rectangle cell(Vector2 tl, int i, int cols) {
    return {tl.x + (i % cols) * (F_SLOT + F_GAP), tl.y + float(i / cols) * (F_SLOT + F_GAP), F_SLOT, F_SLOT};
}
float filterContentW() { return std::max(gridW(F_COLS), F_CHECK + 8 + measure("Filter enabled", 15).x); }
float filterContentH(bool label) { return (label ? F_LABEL_H : 0) + F_CHECK + 8 + gridH(ItemFilter::SLOT_COUNT, F_COLS); }
float pickerW() { return gridW(P_COLS); }
float pickerH() { return P_TITLE_H + gridH(itemCount(), P_COLS); }

std::string fmtPerMin(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f/min", v);
    return buf;
}
} // namespace

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
namespace {
constexpr float PAIR_GAP = 12;
const Vector2 CRAFT_SIZE{300, 600};
const Vector2 MACHINE_SIZE{400, 334};
const Vector2 STORAGE_SIZE{8 * INV_SLOT + INV_PAD * 2, 6 * INV_SLOT + INV_TITLE + INV_PAD};
} // namespace

Vector2 GameScreen::inventorySize() const {
    return {player_.inventory.width() * INV_SLOT + INV_PAD * 2,
            player_.inventory.height() * INV_SLOT + INV_PAD * 2 + INV_TITLE};
}

Rectangle GameScreen::centeredPanel(Vector2 s) const {
    return {std::floor(ui::screenW() / 2 - s.x / 2), std::floor(ui::panelCenterY() - s.y / 2), s.x, s.y};
}

// The smallest rectangle around both panels is centred; tops are aligned.
Rectangle GameScreen::pairRect(Vector2 partner, bool left) const {
    Vector2 inv = inventorySize();
    float w = inv.x + PAIR_GAP + partner.x, h = std::max(inv.y, partner.y);
    float x0 = std::floor(ui::screenW() / 2 - w / 2), y0 = std::floor(ui::panelCenterY() - h / 2);
    return left ? Rectangle{x0, y0, inv.x, inv.y} : Rectangle{x0 + inv.x + PAIR_GAP, y0, partner.x, partner.y};
}

Rectangle GameScreen::inventoryRect() const {
    if (machinePanel_ >= 0) return pairRect(MACHINE_SIZE, true);
    if (storagePanel_ >= 0) return pairRect(STORAGE_SIZE, true);
    if (craftOpen_) return pairRect(CRAFT_SIZE, true);
    return centeredPanel(inventorySize());
}
Rectangle GameScreen::craftRect() const { return invOpen_ ? pairRect(CRAFT_SIZE, false) : centeredPanel(CRAFT_SIZE); }
Rectangle GameScreen::machineRect() const { return invOpen_ ? pairRect(MACHINE_SIZE, false) : centeredPanel(MACHINE_SIZE); }
Rectangle GameScreen::storageRect() const { return invOpen_ ? pairRect(STORAGE_SIZE, false) : centeredPanel(STORAGE_SIZE); }
Rectangle GameScreen::beltFilterRect() const {
    float w = filterContentW() + FP_PICKER_GAP + pickerW() + FP_PAD * 2;
    float h = FP_TITLE + std::max(filterContentH(false), pickerH()) + FP_PAD;
    return centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::panelCenterY()), w, h);
}
Rectangle GameScreen::splitterFilterRect() const {
    float cols = filterContentW() * 3 + FP_COLUMN_GAP * 2;
    float w = cols + FP_PICKER_GAP + pickerW() + FP_PAD * 2;
    float h = FP_TITLE + std::max(filterContentH(true), pickerH()) + FP_PAD;
    return centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::panelCenterY()), w, h);
}

// ---------------------------------------------------------------------------
// Player inventory
// ---------------------------------------------------------------------------
void GameScreen::drawInventory() {
    if (!invOpen_) return;
    Rectangle r = inventoryRect();
    drawPanel(r, "Inventory");
    std::vector<HoverEntry> hover;
    Inventory& inv = player_.inventory;
    for (int y = 0; y < inv.height(); ++y)
        for (int x = 0; x < inv.width(); ++x) {
            Rectangle s{r.x + INV_PAD + x * INV_SLOT, r.y + INV_PAD + INV_TITLE + y * INV_SLOT, INV_SLOT, INV_SLOT};
            drawSlotFrame(s);
            const Slot& sl = inv.at(x, y);
            drawSlotContents(sl, s, 14);
            if (!sl.empty()) hover.push_back({s, sl.item});
        }
    drawHoveredItemTooltip(hover);
}

void GameScreen::inventoryHandle(const Event& e) {
    if (!invOpen_ || e.type != EvType::MouseDown || e.button != MB_LEFT) return;
    bool shift = shiftDown(), ctrl = ctrlDown();
    if (!shift && !ctrl) return;
    Rectangle r = inventoryRect();
    Inventory& inv = player_.inventory;
    for (int y = 0; y < inv.height(); ++y)
        for (int x = 0; x < inv.width(); ++x) {
            Rectangle s{r.x + INV_PAD + x * INV_SLOT, r.y + INV_PAD + INV_TITLE + y * INV_SLOT, INV_SLOT, INV_SLOT};
            if (!inRect(s, e.pos)) continue;
            const Slot& sl = inv.at(x, y);
            if (sl.empty()) return;
            Inventory* dest = nullptr;
            if (storagePanel_ >= 0) dest = &world_.machine(storagePanel_).storage;
            else if (machinePanel_ >= 0) {
                Machine& m = world_.machine(machinePanel_);
                if (MachineBuffer* b = m.inputBuffer(sl.item)) dest = &b->inv;
                else if (MachineBuffer* b2 = m.outputBuffer(sl.item)) dest = &b2->inv;
            }
            if (!dest) return;
            if (shift) moveStack(inv, x, y, *dest, sl.item);
            else moveAllOfType(inv, x, y, *dest, sl.item);
            return;
        }
}

// ---------------------------------------------------------------------------
// Recipe info panel (RecipeUI)
// ---------------------------------------------------------------------------
void GameScreen::drawRecipePanel(const Recipe& rc, Rectangle r) {
    drawPanel(r, rc.name, false);   // info only, nothing to close
    std::vector<HoverEntry> hover;
    text("Inputs:", r.x + 16, r.y + 48, 16, BLACK);
    float x = 90;
    for (auto& p : rc.inputs) {
        Rectangle ir{r.x + x, r.y + 48, 24, 24};
        drawItemIcon(p.first, ir);
        hover.push_back({ir, p.first});
        text("x" + std::to_string(p.second), r.x + x + 28, r.y + 52, 16, BLACK);
        x += 70;
    }
    text("Outputs:", r.x + 16, r.y + 88, 16, BLACK);
    x = 90;
    for (auto& p : rc.outputs) {
        Rectangle ir{r.x + x, r.y + 88, 24, 24};
        drawItemIcon(p.first, ir);
        hover.push_back({ir, p.first});
        text("x" + std::to_string(p.second), r.x + x + 28, r.y + 92, 16, BLACK);
        x += 70;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "Production time: %.2fs", rc.seconds());
    text(buf, r.x + 16, r.y + 130, 16, BLACK);
    drawHoveredItemTooltip(hover);
}

// A recipe as a button in the panel's own colour with a slightly darker
// outline. Selected = the hotbar's yellow, darker yellow outline, bold text.
void GameScreen::drawRecipeButton(Rectangle r, const Recipe& rc, bool selected, std::vector<HoverEntry>& hover) {
    drawButtonBase(r, selected ? ui::CHOICE_SELECTED : ui::CHOICE);
    lineRound(r, 8, 2, selected ? ui::CHOICE_SELECTED_OUTLINE : ui::CHOICE_OUTLINE);
    float x = r.x + 8;
    if (!rc.outputs.empty()) {
        Rectangle ir{x, r.y + (r.height - 24) / 2, 24, 24};
        drawItemIcon(rc.outputs[0].first, ir);
        hover.push_back({ir, rc.outputs[0].first});
        x += 30;
    }
    Vector2 tm = measure(rc.name, 18, selected);
    text(fitText(rc.name, r.x + r.width - 8 - x, 18), x, r.y + (r.height - tm.y) / 2, 18, ui::TEXT, selected);
}

// Recipe details while hovering a recipe button: next to the mouse like a
// tooltip, always fully on screen.
void GameScreen::drawRecipeInfoNearMouse(const Recipe& rc) {
    Vector2 m = ui::mouse();
    const float w = 320, h = 180;
    float x = m.x + 18, y = m.y + 18;
    if (x + w > ui::screenW() - 5) x = m.x - 18 - w;
    if (y + h > ui::screenH() - 5) y = ui::screenH() - 5 - h;
    drawRecipePanel(rc, {std::max(5.0f, x), std::max(5.0f, y), w, h});
}

// ---------------------------------------------------------------------------
// Producing machine panel
// ---------------------------------------------------------------------------
namespace {
struct MachineLayout {
    std::vector<std::pair<Rectangle, const Recipe*>> recipes;
    std::vector<std::pair<Rectangle, int>> inputs, outputs; // slot rect, buffer index
};

MachineLayout layoutMachine(const Machine& m, Rectangle r) {
    MachineLayout L;
    float y = r.y + ui::TITLE_H, right = r.x + r.width - 15;
    if (auto* list = m.recipeChoices()) {
        y += measure("Recipes:", 20, true).y + 8;
        float bw = 150;   // all recipe buttons as wide as the longest name
        for (const Recipe* rc : *list) bw = std::max(bw, measure(rc->name, 18).x + 50);
        for (const Recipe* rc : *list) {
            L.recipes.push_back({{right - bw, y, bw, 34}, rc});
            y += 34 + 8;
        }
    }
    const float pad = 40, slot = 48, spacing = 10;
    for (size_t i = 0; i < m.inputs.size(); ++i)
        L.inputs.push_back({{r.x + pad + float(i) * (slot + spacing), r.y + pad + 34, slot, slot}, int(i)});
    for (size_t i = 0; i < m.outputs.size(); ++i)
        L.outputs.push_back({{r.x + pad + float(i) * (slot + spacing), r.y + r.height - pad - slot, slot, slot}, int(i)});
    return L;
}
} // namespace

void GameScreen::drawMachinePanel() {
    if (machinePanel_ < 0) return;
    Machine& m = world_.machine(machinePanel_);
    Rectangle r = machineRect();
    drawPanel(r, machineSpec(m.type).name);

    // progress bar
    Rectangle bar{r.x + r.width / 2 - 150, r.y + r.height - 20 - 15, 300, 20};
    fillRound(bar, 6, hex("#9A98B5"));
    float progress = (m.processing && m.recipe) ? std::min(1.0f, float(m.timer) / float(m.recipe->ticks)) : 0;
    if (progress > 0) fillRound({bar.x, bar.y, bar.width * progress, bar.height}, 6, rgba(0, 230, 0));
    textCentered(std::to_string(int(progress * 100)) + "%", bar.x + bar.width / 2, bar.y + bar.height / 2, 24, BLACK);

    MachineLayout L = layoutMachine(m, r);
    std::vector<HoverEntry> hover;
    auto drawSlot = [&](Rectangle s, const MachineBuffer& b, double perMin) {
        drawSlotFrame(s);
        const Slot& sl = b.inv.at(0, 0);
        if (!sl.empty()) drawSlotContents(sl, s, 10);
        else drawItemIcon(b.item, {s.x + 5, s.y + 5, s.width - 10, s.height - 10}, 100);
        std::string lbl = fmtPerMin(perMin);
        Vector2 tm = measure(lbl, 16);
        text(lbl, s.x + s.width / 2 - tm.x / 2, s.y - 18, 16, BLACK);
        hover.push_back({s, b.item});
    };
    for (auto& in : L.inputs) {
        const MachineBuffer& b = m.inputs[size_t(in.second)];
        int amt = 0;
        if (m.recipe) for (auto& p : m.recipe->inputs) if (p.first == b.item) amt = p.second;
        drawSlot(in.first, b, m.recipe ? m.recipe->perMinute(amt) : 0);
    }
    for (auto& out : L.outputs) {
        const MachineBuffer& b = m.outputs[size_t(out.second)];
        int amt = 0;
        if (m.recipe) for (auto& p : m.recipe->outputs) if (p.first == b.item) amt = p.second;
        drawSlot(out.first, b, m.recipe ? m.recipe->perMinute(amt) : 0);
    }

    // processing arrow (fades to green while working)
    bool active = m.processing;
    if (!active && m.recipe) {
        active = true;
        for (auto& p : m.recipe->inputs) {
            MachineBuffer* b = m.inputBuffer(p.first);
            if (!b || b->inv.amount(p.first) < p.second) active = false;
        }
    }
    Color target = active ? rgba(0, 230, 0) : rgba(50, 50, 50);
    arrowColor_.r = (unsigned char)(arrowColor_.r + (target.r - arrowColor_.r) * 0.1f);
    arrowColor_.g = (unsigned char)(arrowColor_.g + (target.g - arrowColor_.g) * 0.1f);
    arrowColor_.b = (unsigned char)(arrowColor_.b + (target.b - arrowColor_.b) * 0.1f);
    {
        float aw = 20, ah = 40, x = r.x + 55, y = r.y + r.height / 2 - ah / 2;
        DrawRectangleRec({x, y, aw, ah - aw / 2}, arrowColor_);
        DrawTriangle({x - aw / 2, y + ah - aw / 2}, {x + aw / 2, y + ah}, {x + aw * 1.5f, y + ah - aw / 2}, arrowColor_);
    }

    // recipe list
    const Recipe* hoveredRecipe = nullptr;
    if (m.recipeChoices()) {
        textTopRight("Recipes:", r.x + r.width - 15, r.y + ui::TITLE_H, 20, BLACK, true);
        for (auto& rr : L.recipes) {
            drawRecipeButton(rr.first, *rr.second, rr.second == m.recipe, hover);
            if (hovered(rr.first)) hoveredRecipe = rr.second;
        }
    }
    if (hoveredRecipe) drawRecipeInfoNearMouse(*hoveredRecipe);
    drawHoveredItemTooltip(hover);
}

void GameScreen::machinePanelHandle(const Event& e) {
    if (machinePanel_ < 0 || mode_ == BuildMode::Building || justPlaced_) return;
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return;
    Machine& m = world_.machine(machinePanel_);
    MachineLayout L = layoutMachine(m, machineRect());
    for (auto& rr : L.recipes) {
        if (!inRect(rr.first, e.pos)) continue;
        if (!builder_.setRecipe(m, rr.second)) notify("Not enough inventory space for the machine's items");
        return;
    }
    bool shift = shiftDown(), ctrl = ctrlDown();
    if (!shift && !ctrl) return;
    for (auto& in : L.inputs)
        if (inRect(in.first, e.pos)) { moveStack(m.inputs[size_t(in.second)].inv, 0, 0, player_.inventory); return; }
    for (auto& out : L.outputs)
        if (inRect(out.first, e.pos)) { moveStack(m.outputs[size_t(out.second)].inv, 0, 0, player_.inventory); return; }
}

// ---------------------------------------------------------------------------
// Storage panel
// ---------------------------------------------------------------------------
void GameScreen::drawStoragePanel() {
    if (storagePanel_ < 0) return;
    Inventory& inv = world_.machine(storagePanel_).storage;
    Rectangle r = storageRect();
    drawPanel(r, "Storage");
    std::vector<HoverEntry> hover;
    for (int y = 0; y < inv.height(); ++y)
        for (int x = 0; x < inv.width(); ++x) {
            Rectangle s{r.x + INV_PAD + x * INV_SLOT, r.y + INV_TITLE + y * INV_SLOT, INV_SLOT, INV_SLOT};
            drawSlotFrame(s);
            drawSlotContents(inv.at(x, y), s, 14);
            hover.push_back({s, inv.at(x, y).empty() ? NO_ITEM : inv.at(x, y).item});
        }
    drawHoveredItemTooltip(hover);
}

void GameScreen::storagePanelHandle(const Event& e) {
    if (storagePanel_ < 0 || mode_ == BuildMode::Building || justPlaced_) return;
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return;
    bool shift = shiftDown(), ctrl = ctrlDown();
    if (!shift && !ctrl) return;
    Inventory& inv = world_.machine(storagePanel_).storage;
    Rectangle r = storageRect();
    for (int y = 0; y < inv.height(); ++y)
        for (int x = 0; x < inv.width(); ++x) {
            Rectangle s{r.x + INV_PAD + x * INV_SLOT, r.y + INV_TITLE + y * INV_SLOT, INV_SLOT, INV_SLOT};
            if (!inRect(s, e.pos)) continue;
            ItemId item = inv.at(x, y).item;
            if (inv.at(x, y).empty()) return;
            if (shift) moveStack(inv, x, y, player_.inventory, item);
            else moveAllOfType(inv, x, y, player_.inventory, item);
            return;
        }
}

// ---------------------------------------------------------------------------
// Item filters (belt + splitter) and the item picker
// ---------------------------------------------------------------------------
void GameScreen::drawFilterPanel(Vector2 tl, const ItemFilter& f, const char* label) {
    float x = tl.x, y = tl.y;
    if (label) { text(label, x, y, 18, BLACK, true); y += F_LABEL_H; }
    Rectangle cb{x, y, F_CHECK, F_CHECK};
    DrawRectangleRec(cb, hovered(cb) ? hex("#DDEEFF") : WHITE);
    lineRect(cb, 2, hex("#333333"));
    if (f.enabled) DrawRectangleRec({cb.x + 3, cb.y + 3, cb.width - 6, cb.height - 6}, hex("#3A7D44"));
    text("Filter enabled", cb.x + cb.width + 8, cb.y + 2, 15, BLACK);
    Vector2 grid{x, y + F_CHECK + 8};
    std::vector<HoverEntry> hover;
    for (int i = 0; i < ItemFilter::SLOT_COUNT; ++i) {
        Rectangle c = cell(grid, i, F_COLS);
        ItemId id = f.slots[size_t(i)];
        Color bg = (f.enabled && id) ? hex("#BFE3C4") : hex("#E5E5E5");
        DrawRectangleRec(c, f.enabled && hovered(c) ? brighten(bg, 22) : bg);
        lineRect(c, 2, hex("#888888"));
        if (id) {
            drawItemIcon(id, {c.x + 3, c.y + 3, c.width - 6, c.height - 6}, f.enabled ? 255 : 100);
            hover.push_back({c, id});
        }
    }
    drawHoveredItemTooltip(hover);
}

bool GameScreen::filterPanelClick(Vector2 tl, ItemFilter& f, bool splitter, int side, const Event& e, bool withLabel) {
    float y = tl.y + (withLabel ? F_LABEL_H : 0);
    Rectangle cb{tl.x, y, F_CHECK, F_CHECK};
    bool right = e.button == MB_RIGHT;
    if (inRect(cb, e.pos)) {
        if (!right) f.enabled = !f.enabled;
        return true;
    }
    if (!f.enabled) return false;
    Vector2 grid{tl.x, y + F_CHECK + 8};
    for (int i = 0; i < ItemFilter::SLOT_COUNT; ++i) {
        if (!inRect(cell(grid, i, F_COLS), e.pos)) continue;
        if (right) f.slots[size_t(i)] = NO_ITEM;
        else picker_ = PickerTarget{splitter, side, i};
        return true;
    }
    return false;
}

ItemFilter* GameScreen::pickerFilter() {
    if (!picker_) return nullptr;
    if (picker_->splitter) {
        if (splitterPanel_ < 0) return nullptr;
        return &world_.machine(splitterPanel_).sideFilters[size_t(picker_->side)];
    }
    if (beltPanel_ < 0) return nullptr;
    return &world_.belt(beltPanel_).filter;
}

void GameScreen::drawPicker(Vector2 tl) {
    if (!picker_) return;
    text("Choose an item", tl.x, tl.y, 15, BLACK, true);
    Vector2 grid{tl.x, tl.y + P_TITLE_H};
    std::vector<HoverEntry> hover;
    for (int i = 0; i < itemCount(); ++i) {
        Rectangle c = cell(grid, i, P_COLS);
        DrawRectangleRec(c, hovered(c) ? hex("#FFFFE8") : hex("#FFF3C4"));
        lineRect(c, 2, hex("#886F00"));
        drawItemIcon(ItemId(i + 1), {c.x + 3, c.y + 3, c.width - 6, c.height - 6});
        hover.push_back({c, ItemId(i + 1)});
    }
    drawHoveredItemTooltip(hover);
}

bool GameScreen::pickerClick(Vector2 tl, const Event& e) {
    if (!picker_) return false;
    if (e.button == MB_RIGHT) { picker_.reset(); return true; }
    Vector2 grid{tl.x, tl.y + P_TITLE_H};
    ItemFilter* f = pickerFilter();
    for (int i = 0; i < itemCount(); ++i) {
        if (!inRect(cell(grid, i, P_COLS), e.pos)) continue;
        ItemId id = ItemId(i + 1);
        if (f && !f->contains(id)) f->slots[size_t(picker_->slot)] = id;
        picker_.reset();
        return true;
    }
    Rectangle bounds{grid.x, grid.y, pickerW(), gridH(itemCount(), P_COLS)};
    picker_.reset();
    return inRect(bounds, e.pos);
}

void GameScreen::drawBeltFilterPanel() {
    if (beltPanel_ < 0) return;
    Rectangle r = beltFilterRect();
    drawPanel(r, "Belt Filter");
    Vector2 content{r.x + FP_PAD, r.y + FP_TITLE};
    drawFilterPanel(content, world_.belt(beltPanel_).filter, nullptr);
    drawPicker({content.x + filterContentW() + FP_PICKER_GAP, content.y});
}

void GameScreen::beltFilterHandle(const Event& e) {
    if (beltPanel_ < 0 || mode_ == BuildMode::Building || justPlaced_) return;
    if (e.type != EvType::MouseDown) return;
    Rectangle r = beltFilterRect();
    Vector2 content{r.x + FP_PAD, r.y + FP_TITLE};
    if (picker_) pickerClick({content.x + filterContentW() + FP_PICKER_GAP, content.y}, e);
    else filterPanelClick(content, world_.belt(beltPanel_).filter, false, 0, e, false);
}

void GameScreen::drawSplitterFilterPanel() {
    if (splitterPanel_ < 0) return;
    Machine& s = world_.machine(splitterPanel_);
    Rectangle r = splitterFilterRect();
    drawPanel(r, "Splitter Output Filters");
    float top = r.y + FP_TITLE;
    for (int i = 0; i < 3; ++i)
        drawFilterPanel({r.x + FP_PAD + i * (filterContentW() + FP_COLUMN_GAP), top}, s.sideFilters[size_t(i)], SIDE_LABELS[i]);
    float cols = filterContentW() * 3 + FP_COLUMN_GAP * 2;
    drawPicker({r.x + FP_PAD + cols + FP_PICKER_GAP, top});
}

void GameScreen::splitterFilterHandle(const Event& e) {
    if (splitterPanel_ < 0 || mode_ == BuildMode::Building || justPlaced_) return;
    if (e.type != EvType::MouseDown) return;
    Machine& s = world_.machine(splitterPanel_);
    Rectangle r = splitterFilterRect();
    float top = r.y + FP_TITLE;
    float cols = filterContentW() * 3 + FP_COLUMN_GAP * 2;
    if (picker_) { pickerClick({r.x + FP_PAD + cols + FP_PICKER_GAP, top}, e); return; }
    for (int i = 0; i < 3; ++i)
        if (filterPanelClick({r.x + FP_PAD + i * (filterContentW() + FP_COLUMN_GAP), top}, s.sideFilters[size_t(i)], true, i, e, true))
            return;
}

// ---------------------------------------------------------------------------
// Hand crafting
// ---------------------------------------------------------------------------
void GameScreen::drawCraftPanel() {
    if (!craftOpen_) return;
    Rectangle r = craftRect();
    float bottom = r.y + r.height;
    drawPanel(r, "Handcrafting");

    const auto& recipes = handcraftRecipes();
    const float rowH = 45;
    Rectangle viewport{r.x, r.y + ui::TITLE_H, r.width - 20, (bottom - 320) - (r.y + ui::TITLE_H)};
    float content = float(recipes.size()) * rowH;
    craftScroll_ = clampScroll(craftScroll_, content, viewport.height);

    std::vector<HoverEntry> hover;
    const Recipe* hoveredRecipe = nullptr;
    ui::beginScissor({viewport.x, viewport.y, viewport.width, viewport.height});
    for (size_t i = 0; i < recipes.size(); ++i) {
        Rectangle row{r.x + 10, viewport.y - craftScroll_ + float(i) * rowH, r.width - 34, 40};
        if (row.y + row.height < viewport.y || row.y > viewport.y + viewport.height) continue;
        // only light up the part of the row that is visible
        bool visible = inRect(viewport, ui::mouse());
        if (!visible) ui::setHoverBlocked(true);
        drawRecipeButton(row, *recipes[i], int(i) == player_.selectedRecipe, hover);
        if (!visible) ui::setHoverBlocked(anyModalOpen());
        if (hovered(row) && visible) hoveredRecipe = recipes[i];
    }
    EndScissorMode();
    if (content > viewport.height)
        drawScrollbar({r.x + r.width - 16, viewport.y, 6, viewport.height}, craftScroll_, content, viewport.height);

    const Recipe* sel = selectedHandRecipe(player_);
    if (sel) drawRecipePanel(*sel, {r.x + 20, bottom - 320, r.width - 40, 180});

    // Produce button
    CraftStatus st = sel ? craftStatus(player_.inventory, sel) : CraftStatus::NoInputs;
    bool producing = player_.craftMode != CraftMode::None;
    float bw = 180, bh = 40;
    Color bc = rgba(120, 120, 120);
    if (producing && st == CraftStatus::Ok) { bw *= 0.95f; bh *= 0.95f; bc = rgba(0, 180, 0); }
    else if (st == CraftStatus::Ok) bc = rgba(0, 230, 0);
    else if (st == CraftStatus::NoSpace) bc = rgba(255, 165, 0);
    Rectangle prod{r.x + r.width / 2 - bw / 2, bottom - 120, bw, bh};
    drawButtonBase(prod, bc);
    textCentered("Produce", prod.x + prod.width / 2, prod.y + prod.height / 2, 20, WHITE);

    // progress
    Rectangle bar{r.x + r.width / 2 - 90, bottom - 65, 180, 20};
    fillRound(bar, 6, hex("#9A98B5"));
    float progress = (sel && producing) ? float(player_.craftTicks) / float(sel->ticks) : 0;
    if (progress > 0) fillRound({bar.x, bar.y, bar.width * std::min(progress, 1.0f), bar.height}, 6, rgba(0, 230, 0));

    // cancel
    float cw = producing ? 80 : 72, ch = producing ? 20 : 18;
    Rectangle cancel{r.x + r.width / 2 - cw / 2, bottom - 30, cw, ch};
    drawButtonBase(cancel, producing ? rgba(200, 0, 0) : rgba(120, 0, 0));
    textCentered("Cancel", cancel.x + cancel.width / 2, cancel.y + cancel.height / 2, 18, WHITE);

    drawHoveredItemTooltip(hover);
    if (hoveredRecipe) drawRecipeInfoNearMouse(*hoveredRecipe);
}

void GameScreen::craftHandleClick(const Event& e) {
    Rectangle r = craftRect();
    float bottom = r.y + r.height;
    Rectangle prod{r.x + r.width / 2 - 90, bottom - 120, 180, 40};
    if (inRect(prod, e.pos)) {
        const Recipe* sel = selectedHandRecipe(player_);
        if (sel && craftStatus(player_.inventory, sel) == CraftStatus::Ok) {
            player_.craftMode = CraftMode::Single;
            player_.craftTicks = 0;
        }
        return;
    }
    Rectangle cancel{r.x + r.width / 2 - 40, bottom - 30, 80, 20};
    if (inRect(cancel, e.pos)) {
        player_.craftMode = CraftMode::None;
        player_.craftTicks = 0;
        return;
    }
    Rectangle viewport{r.x, r.y + ui::TITLE_H, r.width - 20, (bottom - 320) - (r.y + ui::TITLE_H)};
    if (!inRect(viewport, e.pos)) return;
    const auto& recipes = handcraftRecipes();
    for (size_t i = 0; i < recipes.size(); ++i) {
        Rectangle row{r.x + 10, viewport.y - craftScroll_ + float(i) * 45, r.width - 34, 40};
        if (inRect(row, e.pos)) {
            player_.selectedRecipe = int(i);
            player_.craftMode = CraftMode::None;
            player_.craftTicks = 0;
            return;
        }
    }
}
