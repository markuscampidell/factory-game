// game/GameBlueprints.cpp - copying/pasting many machines and belts at once
// (Ctrl+C / Ctrl+V) and the blueprint list (Ctrl+B / B).
#include "GameScreen.hpp"
#include "Assets.hpp"
#include <algorithm>

namespace {
const Color SELECT_FILL{90, 170, 255, 45};
const Color SELECT_LINE{90, 170, 255, 200};
const Color CUT_FILL{255, 70, 70, 50};
const Color CUT_LINE{255, 70, 70, 210};
const Color BAD_RED{255, 110, 110, 255};

std::string objects(int n) { return std::to_string(n) + (n == 1 ? " object" : " objects"); }

constexpr float LIST_W = 560, LIST_H = 470, ROW_H = 56, LIST_TOP = 60;
} // namespace

// ---------------------------------------------------------------------------
// State changes
// ---------------------------------------------------------------------------
void GameScreen::startSelecting(bool forList) {
    if (mapMode()) { notify("Zoom in to copy"); return; }
    closeAllPanels();
    mode_ = BuildMode::Building;
    placingBelt_ = false;
    bpMode_ = BpMode::Selecting;
    bpForList_ = forList;
    bpDragging_ = false;
    notify(forList ? "New blueprint: drag a rectangle over what you want to save"
                   : "Copy: drag a rectangle over what you want to copy");
}

void GameScreen::startCutting() {
    if (mapMode()) { notify("Zoom in to delete"); return; }
    closeAllPanels();
    mode_ = BuildMode::Deleting;
    placingBelt_ = false;
    bpMode_ = BpMode::Cutting;
    bpDragging_ = false;
}

// Delete everything in the dragged rectangle - only if all refunds fit.
void GameScreen::finishCut(Vec2i end) {
    bpDragging_ = false;
    ItemAmounts got;
    int n = 0;
    Vec2i a = bpDragStart_;
    Vector2 center{float((a.x + end.x + 1) * CELL_SIZE) / 2, float((a.y + end.y + 1) * CELL_SIZE) / 2};
    switch (builder_.deleteArea(a, end, &got, &n)) {
        case Builder::AreaDelete::Nothing:
            floatText(center.x, center.y, "Nothing to delete here", BAD_RED);
            break;
        case Builder::AreaDelete::NoSpace:
            floatText(center.x, center.y, "Not enough inventory space - nothing was deleted", BAD_RED);
            break;
        case Builder::AreaDelete::Ok:
            showItemDelta(center.x, center.y, got);
            break;
    }
}

void GameScreen::startPlacing(const Blueprint& bp) {
    if (mapMode()) { notify("Zoom in to paste"); return; }
    closeAllPanels();
    mode_ = BuildMode::Building;
    placingBelt_ = false;
    bpMode_ = BpMode::Placing;
    bpDragging_ = false;
    placing_ = bp;
}

void GameScreen::finishSelection(Vec2i end) {
    bpDragging_ = false;
    Blueprint bp = captureBlueprint(world_, bpDragStart_, end);
    if (bp.empty()) { notify("Nothing selected - drag over machines or belts"); return; }
    if (bpForList_) {
        // don't place anything yet: ask for a name first
        int n = 1;
        auto taken = [&](const std::string& nm) {
            return std::any_of(blueprints_.begin(), blueprints_.end(), [&](const Blueprint& b) { return b.name == nm; });
        };
        while (taken("Blueprint " + std::to_string(n))) ++n;
        bpPending_ = bp;
        bpPendingA_ = bpDragStart_;
        bpPendingB_ = end;
        bpSaveError_.clear();
        bpSaveOpen_ = true;
        Rectangle r = bpSaveRect();
        bpNameInput_ = TextInput({r.x + 30, r.y + 110, r.width - 60, 40}, "Blueprint " + std::to_string(n));
        return;
    }
    bp.name = "Clipboard";
    clipboard_ = bp;
    notify("Copied " + objects(bp.count()) + " - click to place, Ctrl+V to paste again");
    startPlacing(bp);
}

// ---------------------------------------------------------------------------
// "Save blueprint" dialog
// ---------------------------------------------------------------------------
Rectangle GameScreen::bpSaveRect() const {
    return centeredRect(std::floor(cam_.w / 2), std::floor(cam_.h / 2) - 40, 440, 250);
}

void GameScreen::savePendingBlueprint() {
    std::string name = bpNameInput_ ? bpNameInput_->text : "";
    size_t a = name.find_first_not_of(' ');
    if (a == std::string::npos) { bpSaveError_ = "Please enter a name"; return; }
    name = name.substr(a, name.find_last_not_of(' ') - a + 1);
    for (auto& b : blueprints_)
        if (b.name == name) { bpSaveError_ = "A blueprint with this name already exists"; return; }
    bpPending_.name = name;
    blueprints_.push_back(bpPending_);
    save::saveBlueprints(blueprints_);
    notify("Saved blueprint '" + name + "' (press B to see your blueprints)");
    bpSaveOpen_ = false;
    bpNameInput_.reset();
    resetBuildState();
}

bool GameScreen::bpSaveHandle(const Event& e) {
    if (!bpSaveOpen_) return false;
    Rectangle r = bpSaveRect();
    Rectangle reselect{r.x + 30, r.y + r.height - 62, 150, 40};
    Rectangle saveBtn{r.x + r.width - 30 - 190, r.y + r.height - 62, 190, 40};
    auto cancel = [&]() {
        bpSaveOpen_ = false;
        bpNameInput_.reset();
        resetBuildState();          // ESC / click outside: cancel everything
    };
    if (e.type == EvType::MouseDown && (!inRect(r, e.pos) || inRect(panelCloseRect(r), e.pos))) { cancel(); return true; }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(reselect, e.pos)) {
            bpSaveOpen_ = false;
            bpNameInput_.reset();
            bpMode_ = BpMode::Selecting;   // drag a new rectangle
            bpDragging_ = false;
            notify("Drag a new rectangle for the blueprint");
            return true;
        }
        if (inRect(saveBtn, e.pos)) { savePendingBlueprint(); return true; }
    }
    if (bpNameInput_) {
        bpNameInput_->handle(e);
        if (bpNameInput_ && bpNameInput_->cancelled) { cancel(); return true; }
        if (bpNameInput_ && bpNameInput_->submitted) { bpNameInput_->submitted = false; savePendingBlueprint(); }
    }
    return true;   // modal
}

void GameScreen::drawBpSaveDialog() {
    if (!bpSaveOpen_) return;
    Rectangle r = bpSaveRect();
    drawModalShade();
    drawPanel(r, "Save blueprint");
    std::string info = std::to_string(bpPending_.w) + "x" + std::to_string(bpPending_.h) + ", " + objects(bpPending_.count());
    textCentered(info, r.x + r.width / 2, r.y + 56, 16, rgba(50, 50, 50));
    textCentered(fitText("Cost: " + costText(bpPending_.cost()), r.width - 40, 15), r.x + r.width / 2, r.y + 78, 15, rgba(50, 50, 50));
    text("Name:", r.x + 30, r.y + 92, 14, ui::TEXT_DIM);
    if (bpNameInput_) {
        bpNameInput_->update(GetFrameTime());
        bpNameInput_->draw();
    }
    if (!bpSaveError_.empty()) textCentered(bpSaveError_, r.x + r.width / 2, r.y + 165, 15, rgba(170, 20, 20));
    Rectangle reselect{r.x + 30, r.y + r.height - 62, 150, 40};
    Rectangle saveBtn{r.x + r.width - 30 - 190, r.y + r.height - 62, 190, 40};
    drawButton(reselect, "Reselect", ui::BTN);
    drawButton(saveBtn, "Save blueprint", ui::BTN_OK);
    textCentered("ESC or click outside to cancel", r.x + r.width / 2, r.y + r.height - 11, 12, ui::TEXT_DIM);
}

void GameScreen::placeHeldBlueprint(Vector2 mouse) {
    Vec2i tl = Builder::blueprintTopLeft(placing_, screenToTile(mouse));
    bool replace = shiftDown();   // Shift: replace whatever is underneath
    Builder::BlueprintCheck c = builder_.checkBlueprint(placing_, tl, replace);
    Vector2 w = cam_.screenToWorld(mouse);
    switch (c.status) {
        case PlaceStatus::Blocked: floatText(w.x, w.y, "Something is in the way (Shift: replace it)", BAD_RED); return;
        case PlaceStatus::NoOre: floatText(w.x, w.y, "A miner here would have no ore under it", BAD_RED); return;
        case PlaceStatus::NoFunds: floatText(w.x, w.y, "Not enough items in your inventory", BAD_RED); return;
        case PlaceStatus::NoSpace: floatText(w.x, w.y, "Not enough inventory space for what gets replaced", BAD_RED); return;
        case PlaceStatus::Undiscovered: floatText(w.x, w.y, "You haven't discovered this area yet", BAD_RED); return;
        default: break;
    }
    ItemAmounts delta;
    if (builder_.placeBlueprint(placing_, tl, replace, &delta)) showItemDelta(w.x, w.y, delta);
}

bool GameScreen::overUi(Vector2 p) const {
    if (mouseOverOpenPanel(p)) return true;
    for (auto& r : hotbarRects()) if (inRect(r, p)) return true;
    return overHudButtons(p);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool GameScreen::blueprintHandle(const Event& e) {
    if (bpSaveHandle(e)) return true;
    // delete confirmation (modal)
    if (bpDeleteConfirm_) {
        DialogResult r = bpDeleteConfirm_->handle(e);
        if (r == DialogResult::Yes && bpDeleteIndex_ >= 0 && bpDeleteIndex_ < int(blueprints_.size())) {
            blueprints_.erase(blueprints_.begin() + bpDeleteIndex_);
            save::saveBlueprints(blueprints_);
        }
        if (r != DialogResult::None) { bpDeleteConfirm_.reset(); bpDeleteIndex_ = -1; }
        return true;
    }

    if (e.type == EvType::KeyDown) {
        bool ctrl = ctrlDown();
        if (ctrl && e.key == KEY_C) { startSelecting(false); return true; }
        // Ctrl+X: area delete (handled here so it never toggles delete mode off)
        if (ctrl && e.key == KEY_X) { startCutting(); return true; }
        if (ctrl && e.key == KEY_B) { startSelecting(true); return true; }
        if (ctrl && e.key == KEY_V) {
            if (clipboard_.empty()) notify("The clipboard is empty - press Ctrl+C first");
            else startPlacing(clipboard_);
            return true;
        }
        if (!ctrl && e.key == KEY_B) {
            if (bpListOpen_) closePanel(Panel::Blueprints);
            else openBlueprintList();
            return true;
        }
        if (e.key == KEY_ESCAPE && (bpMode_ != BpMode::None || bpListOpen_)) {
            if (!closeTopPanel()) resetBuildState();   // the list first, then copy/paste mode
            return true;
        }
        return false;
    }

    // the blueprint list panel
    if (bpListOpen_) {
        Rectangle r = bpListRect();
        if (e.type == EvType::Wheel && inRect(r, e.pos)) {
            float content = float(blueprints_.size()) * ROW_H;
            bpListScroll_ = clampScroll(bpListScroll_ - e.wheel * 40, content, r.height - LIST_TOP - 16);
            return true;
        }
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(r, e.pos)) {
            Rectangle close = panelCloseRect(r);
            Rectangle add{close.x - 10 - 90, r.y + 6, 90, 32};
            if (inRect(add, e.pos)) { startSelecting(true); return true; }
            if (inRect(close, e.pos)) { closePanel(Panel::Blueprints); return true; }
            float top = r.y + LIST_TOP;
            for (size_t i = 0; i < blueprints_.size(); ++i) {
                float y = top + float(i) * ROW_H - bpListScroll_;
                if (y < top - 1 || y + ROW_H > r.y + r.height - 8) continue;
                Rectangle use{r.x + r.width - 16 - 70 - 8 - 70, y + 10, 70, 32};
                Rectangle del{r.x + r.width - 16 - 70, y + 10, 70, 32};
                if (inRect(use, e.pos)) { startPlacing(blueprints_[i]); return true; }
                if (inRect(del, e.pos)) {
                    bpDeleteIndex_ = int(i);
                    bpDeleteConfirm_ = std::make_unique<ConfirmDialog>("Delete '" + blueprints_[i].name + "'?");
                    return true;
                }
            }
            return true; // clicks inside the panel never reach the world
        }
    }

    if (bpMode_ == BpMode::None) return false;

    // right click: back to normal building (or normal deleting after Ctrl+X)
    if (e.type == EvType::MouseDown && e.button == MB_RIGHT) {
        mode_ = bpMode_ == BpMode::Cutting ? BuildMode::Deleting : BuildMode::Building;
        bpMode_ = BpMode::None;
        bpDragging_ = false;
        return true;
    }
    if (bpMode_ == BpMode::Cutting) {
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && !overUi(e.pos)) {
            bpDragging_ = true;
            bpDragStart_ = screenToTile(e.pos);
            return true;
        }
        if (e.type == EvType::MouseUp && e.button == MB_LEFT && bpDragging_) {
            finishCut(screenToTile(e.pos));
            return true;
        }
    }
    if (bpMode_ == BpMode::Selecting) {
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && !overUi(e.pos)) {
            bpDragging_ = true;
            bpDragStart_ = screenToTile(e.pos);
            return true;
        }
        if (e.type == EvType::MouseUp && e.button == MB_LEFT && bpDragging_) {
            finishSelection(screenToTile(e.pos));
            return true;
        }
    } else if (bpMode_ == BpMode::Placing) {
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && !overUi(e.pos)) {
            placeHeldBlueprint(e.pos);
            return true;
        }
    }
    return false;
}

void GameScreen::openBlueprintList() {
    resetBuildState();
    makeRoomFor(Panel::Blueprints);
    bpListOpen_ = true;
    bpListScroll_ = 0;
}

Rectangle GameScreen::bpListRect() const {
    return centeredRect(std::floor(cam_.w / 2), std::floor(cam_.h / 2) - 20, LIST_W, LIST_H);
}

// ---------------------------------------------------------------------------
// Drawing (world space): selection rectangle / blueprint ghost
// ---------------------------------------------------------------------------
void GameScreen::drawBlueprintMode() {
    float t = 1.0f / cam_.zoom;
    if (bpMode_ == BpMode::Selecting || bpMode_ == BpMode::Cutting) {
        bool cut = bpMode_ == BpMode::Cutting;
        Vec2i a = bpDragging_ ? bpDragStart_ : mouseTile();
        Vec2i b = mouseTile();
        if (bpSaveOpen_) { a = bpPendingA_; b = bpPendingB_; }
        Selection s = makeSelection(a, b);   // never bigger than MAX_SELECTION
        Rectangle r{float(s.x0 * CELL_SIZE), float(s.y0 * CELL_SIZE), float(s.w() * CELL_SIZE), float(s.h() * CELL_SIZE)};
        // highlight exactly what would be taken
        if (bpDragging_ || bpSaveOpen_) {
            const Color hl = cut ? Color{255, 60, 60, 70} : Color{255, 255, 255, 60};
            std::vector<int> belts, machines;
            gatherSelection(world_, s, belts, machines);
            for (int id : belts) {
                Vec2i p = world_.belt(id).pos;
                DrawRectangleRec({float(p.x * CELL_SIZE), float(p.y * CELL_SIZE), CELL_SIZE, CELL_SIZE}, hl);
            }
            for (int id : machines) {
                RectF mr = world_.machine(id).pixelRect();
                DrawRectangleRec({mr.x, mr.y, mr.w, mr.h}, hl);
            }
        }
        DrawRectangleRec(r, cut ? CUT_FILL : SELECT_FILL);
        DrawRectangleLinesEx(r, 2 * t, cut ? CUT_LINE : SELECT_LINE);
        return;
    }

    // Placing: ghost of everything at the mouse
    Vec2i tl = Builder::blueprintTopLeft(placing_, mouseTile());
    Builder::BlueprintCheck c = builder_.checkBlueprint(placing_, tl, shiftDown());
    const Color red{255, 0, 0, 110}, purple{128, 0, 128, 110}, yellow{255, 255, 0, 90}, orange{255, 165, 0, 100};
    const Color dark{20, 20, 20, 170};   // undiscovered area
    auto itemTint = [&](Rectangle r) {   // the whole placement fails for item reasons
        if (c.status == PlaceStatus::NoFunds) DrawRectangleRec(r, yellow);
        else if (c.status == PlaceStatus::NoSpace) DrawRectangleRec(r, orange);
    };
    for (size_t i = 0; i < placing_.belts.size(); ++i) {
        const BlueprintBelt& b = placing_.belts[i];
        Vec2i p = tl + b.off;
        Rectangle r{float(p.x * CELL_SIZE), float(p.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
        drawTexture(assets::belt(b.mask, b.dir), r, Color{255, 255, 255, 160});
        if (c.belts[i] == PlaceStatus::Blocked) DrawRectangleRec(r, red);
        else if (c.belts[i] == PlaceStatus::Undiscovered) DrawRectangleRec(r, dark);
        else itemTint(r);
    }
    for (size_t i = 0; i < placing_.machines.size(); ++i) {
        const BlueprintMachine& m = placing_.machines[i];
        const MachineSpec& s = machineSpec(m.type);
        Vec2i p = tl + m.off;
        Rectangle r{float(p.x * CELL_SIZE), float(p.y * CELL_SIZE), float(s.w * CELL_SIZE), float(s.h * CELL_SIZE)};
        if (const Texture2D* tex = assets::machine(m.type))
            drawTexture(*tex, r, Color{255, 255, 255, 140}, m.type == MachineType::Splitter ? 90.0f * float(int(m.dir)) : 0.0f);
        else
            DrawRectangleRec(r, Color{150, 150, 170, 120});
        if (const Recipe* rc = findRecipe(m.recipeId); showOutputs_ && rc && !rc->outputs.empty())   // Alt mode
            drawItemIcon(rc->outputs[0].first, {r.x + r.width / 2 - 16, r.y + r.height / 2 - 16, 32, 32}, 160);
        if (c.machines[i] == PlaceStatus::Blocked) DrawRectangleRec(r, red);
        else if (c.machines[i] == PlaceStatus::NoOre) DrawRectangleRec(r, purple);
        else if (c.machines[i] == PlaceStatus::Undiscovered) DrawRectangleRec(r, dark);
        else itemTint(r);
    }
    Rectangle outline{float(tl.x * CELL_SIZE), float(tl.y * CELL_SIZE), float(placing_.w * CELL_SIZE), float(placing_.h * CELL_SIZE)};
    DrawRectangleLinesEx(outline, 2 * t, SELECT_LINE);
}

// ---------------------------------------------------------------------------
// Drawing (screen space): "Needs: ..." while holding something to build
// ---------------------------------------------------------------------------
void GameScreen::drawCostInfo() {
    if (mapMode() || mode_ != BuildMode::Building || bpSaveOpen_) return;
    // only what is actually new: identical objects underneath are kept for free
    ItemAmounts cost;
    bool replace = shiftDown();
    if (bpMode_ == BpMode::Placing) {
        cost = builder_.checkBlueprint(placing_, Builder::blueprintTopLeft(placing_, mouseTile()), replace).cost;
    } else if (bpMode_ != BpMode::None) {
        return;
    } else if (selected_ == Buildable::Belt) {
        // one belt, or the whole line while dragging
        Vec2i mt = mouseTile();
        builder_.beltAffordability(builder_.planBelts(placingBelt_ ? beltStart_ : mt, mt, beltDir_), &cost);
    } else {
        builder_.checkMachine(toMachine(selected_), mouseTile(), Dir(splitterRot_), replace, &cost);
    }
    bool enough = player_.inventory.hasEnough(cost);
    std::string line = "Needs: " + costText(cost);
    Color c = enough ? Color{20, 90, 30, 255} : Color{170, 20, 20, 255};
    float w = measure(line, 18).x + 28;
    Rectangle r = centeredRect(cam_.w / 2, 120, w, 32);
    fillRound(r, 10, ui::PANEL_BG);
    textCentered(line, r.x + r.width / 2, r.y + 16, 18, c, true);
}

// ---------------------------------------------------------------------------
// Drawing (screen space): info box while copying + the list panel
// ---------------------------------------------------------------------------
void GameScreen::drawBlueprintList() {
    // info box while selecting / cutting (placing shows drawCostInfo())
    if ((bpMode_ == BpMode::Selecting || bpMode_ == BpMode::Cutting) && !mapMode() && !bpSaveOpen_) {
        std::string line1, line2;
        if (bpMode_ == BpMode::Selecting) {
            line1 = bpForList_ ? "New blueprint: drag a rectangle" : "Copy: drag a rectangle";
            line1 += " (max " + std::to_string(MAX_SELECTION) + " x " + std::to_string(MAX_SELECTION) + ")";
            line2 = "Right click: back to building   ESC: cancel";
        } else {
            line1 = "Delete: drag a rectangle (max " + std::to_string(MAX_SELECTION) + " x " + std::to_string(MAX_SELECTION) + ")";
            line2 = "Right click: normal deleting   ESC: cancel";
        }
        float w = std::max(measure(line1, 18).x, measure(line2, 16).x) + 28;
        Rectangle r = centeredRect(cam_.w / 2, 128, w, 52);   // top centre, out of the way
        fillRound(r, 10, ui::PANEL_BG);
        textCentered(line1, r.x + r.width / 2, r.y + 15, 18, ui::TEXT, true);
        textCentered(line2, r.x + r.width / 2, r.y + 37, 16, ui::TEXT_DIM);
    }
    if (!bpListOpen_) return;   // (the save dialog is drawn by drawModals)
    Rectangle r = bpListRect();
    drawPanel(r, "Blueprints");
    Rectangle add{panelCloseRect(r).x - 10 - 90, r.y + 6, 90, 32};   // left of the X
    drawButton(add, "+ Add", ui::BTN_OK);

    float top = r.y + LIST_TOP;
    Rectangle viewport{r.x, top, r.width, r.y + r.height - 8 - top};
    if (blueprints_.empty()) {
        textCentered("No blueprints yet.", r.x + r.width / 2, top + 60, 20, BLACK);
        textCentered("Press '+ Add' or Ctrl + B and drag over your machines.", r.x + r.width / 2, top + 90, 16, ui::TEXT_DIM);
        return;
    }
    float content = float(blueprints_.size()) * ROW_H;
    bpListScroll_ = clampScroll(bpListScroll_, content, viewport.height);
    BeginScissorMode(int(viewport.x), int(viewport.y), int(viewport.width), int(viewport.height));
    for (size_t i = 0; i < blueprints_.size(); ++i) {
        const Blueprint& bp = blueprints_[i];
        float y = top + float(i) * ROW_H - bpListScroll_;
        if (y + ROW_H < viewport.y || y > viewport.y + viewport.height) continue;
        Rectangle row{r.x + 14, y + 4, r.width - 28, ROW_H - 8};
        fillRound(row, 8, ui::ROW_BG);
        float textW = row.width - 2 * 78 - 20;
        text(fitText(bp.name, textW, 20), row.x + 10, row.y + 4, 20, BLACK, true);
        std::string info = std::to_string(bp.w) + "x" + std::to_string(bp.h) + ", " + objects(bp.count()) +
                           " - " + costText(bp.cost());
        text(fitText(info, textW, 14), row.x + 10, row.y + 28, 14, rgba(50, 50, 50));
        Rectangle use{r.x + r.width - 16 - 70 - 8 - 70, y + 10, 70, 32};
        Rectangle del{r.x + r.width - 16 - 70, y + 10, 70, 32};
        drawButton(use, "Use", ui::BTN);
        drawButton(del, "Delete", ui::BTN_DANGER);
    }
    EndScissorMode();
    if (content > viewport.height)
        drawScrollbar({r.x + r.width - 8, viewport.y, 5, viewport.height}, bpListScroll_, content, viewport.height);
}
