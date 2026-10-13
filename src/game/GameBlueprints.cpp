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
constexpr float BOOK_H = 46, EMPTY_BOOK_H = 30, BOOK_INDENT = 28, DRAG_MIN = 6;
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
    return centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::screenH() / 2) - 40, 440, 250);
}

void GameScreen::savePendingBlueprint() {
    std::string name = bpNameInput_ ? bpNameInput_->text : "";
    size_t a = name.find_first_not_of(' ');
    if (a == std::string::npos) { bpSaveError_ = "Please enter a name"; return; }
    name = name.substr(a, name.find_last_not_of(' ') - a + 1);
    for (auto& b : blueprints_)
        if (b.name == name) { bpSaveError_ = "A blueprint with this name already exists"; return; }
    bpPending_.name = name;
    bpPending_.book = 0;   // new blueprints start outside the books
    blueprints_.push_back(bpPending_);
    saveBlueprintList();
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
    Vector2 w = cam_.screenToWorld(ui::toReal(mouse));
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
    if (bpBookDialogHandle(e)) return true;
    // delete confirmation (modal) - a blueprint, or a book with everything in it
    if (bpDeleteConfirm_) {
        DialogResult r = bpDeleteConfirm_->handle(e);
        if (r == DialogResult::Yes) {
            if (bpDeleteBookId_ > 0) {
                int id = bpDeleteBookId_;
                blueprints_.erase(std::remove_if(blueprints_.begin(), blueprints_.end(), [&](const Blueprint& b) { return b.book == id; }),
                                  blueprints_.end());
                bpBooks_.erase(std::remove_if(bpBooks_.begin(), bpBooks_.end(), [&](const BlueprintBook& b) { return b.id == id; }),
                               bpBooks_.end());
                bpFold_.setOpen(bookKey(id), false);
                saveBlueprintList();
            } else if (bpDeleteIndex_ >= 0 && bpDeleteIndex_ < int(blueprints_.size())) {
                blueprints_.erase(blueprints_.begin() + bpDeleteIndex_);
                saveBlueprintList();
            }
        }
        if (r != DialogResult::None) { bpDeleteConfirm_.reset(); bpDeleteIndex_ = -1; bpDeleteBookId_ = 0; }
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
        // drag & drop: let go over a book (or a blueprint in it) to put the
        // blueprint into that book, anywhere else in the list to take it out
        if (e.type == EvType::MouseUp && e.button == MB_LEFT && bpDragIndex_ >= 0) {
            int i = bpDragIndex_;
            bool moved = bpDragging();
            bpDragIndex_ = -1;
            if (moved && i < int(blueprints_.size())) {
                int target = bpDropBook(e.pos);
                if (target >= 0 && target != blueprints_[size_t(i)].book) {
                    blueprints_[size_t(i)].book = target;
                    if (target > 0) bpFold_.setOpen(bookKey(target), true);
                    saveBlueprintList();
                }
            }
            return true;
        }
        if (e.type == EvType::Wheel && inRect(r, e.pos)) {
            auto rows = bpListRows();
            float content = rows.empty() ? 0 : rows.back().y + rows.back().h;
            bpListScroll_ = clampScroll(bpListScroll_ - e.wheel * 40, content, r.height - LIST_TOP - 8);
            return true;
        }
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(r, e.pos)) {
            Rectangle close = panelCloseRect(r);
            Rectangle add{close.x - 10 - 90, r.y + 6, 90, 32};
            Rectangle addBook{r.x + 12, r.y + 6, 190, 32};
            if (inRect(add, e.pos)) { startSelecting(true); return true; }
            if (inRect(addBook, e.pos)) { openBookDialog(0); return true; }
            if (inRect(close, e.pos)) { closePanel(Panel::Blueprints); return true; }
            auto rows = bpListRows();
            const BpRow* row = bpRowAt(rows, e.pos);
            if (!row) return true;
            Rectangle rr = bpRowRect(*row);
            Rectangle del{rr.x + rr.width - 8 - 70, rr.y + (rr.height - 32) / 2, 70, 32};
            if (row->kind == BpRowKind::Book) {
                const BlueprintBook& bk = bpBooks_[size_t(row->index)];
                Rectangle rename{del.x - 8 - 80, del.y, 80, 32};
                if (inRect(del, e.pos)) {
                    int n = int(std::count_if(blueprints_.begin(), blueprints_.end(), [&](const Blueprint& b) { return b.book == bk.id; }));
                    bpDeleteBookId_ = bk.id;
                    bpDeleteConfirm_ = std::make_unique<ConfirmDialog>("Delete book '" + bk.name + "'?");
                    if (n > 0)
                        bpDeleteConfirm_->warning = "The " + std::to_string(n) + (n == 1 ? " blueprint" : " blueprints") +
                                                    " in it will be deleted too!";
                } else if (inRect(rename, e.pos)) {
                    openBookDialog(bk.id);
                } else {
                    bpFold_.toggle(bookKey(bk.id));   // click on the headline
                    auto after = bpListRows();
                    float content = after.empty() ? 0 : after.back().y + after.back().h;
                    bpListScroll_ = clampScroll(bpListScroll_, content, r.height - LIST_TOP - 8);
                }
                return true;
            }
            if (row->kind == BpRowKind::Blueprint) {
                size_t i = size_t(row->index);
                Rectangle use{del.x - 8 - 70, del.y, 70, 32};
                if (inRect(use, e.pos)) { startPlacing(blueprints_[i]); return true; }
                if (inRect(del, e.pos)) {
                    bpDeleteIndex_ = int(i);
                    bpDeleteBookId_ = 0;
                    bpDeleteConfirm_ = std::make_unique<ConfirmDialog>("Delete '" + blueprints_[i].name + "'?");
                    return true;
                }
                bpDragIndex_ = int(i);   // pick it up (a drag starts once the mouse moves)
                bpDragFrom_ = e.pos;
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
    return centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::panelCenterY()), LIST_W, LIST_H);
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
    Rectangle r = centeredRect(ui::screenW() / 2, 120, w, 32);
    fillRound(r, 10, ui::PANEL_BG);
    textCentered(line, r.x + r.width / 2, r.y + 16, 18, c, true);

    // belts and splitters can be turned: say how, right under "Needs: ..."
    if (bpMode_ == BpMode::None && (selected_ == Buildable::Belt || selected_ == Buildable::Splitter)) {
        std::string hint = "R: rotate    Shift + R: rotate back    T: turn around";
        Rectangle h = centeredRect(ui::screenW() / 2, 156, measure(hint, 16).x + 24, 28);
        fillRound(h, 10, Color{ui::PANEL_BG.r, ui::PANEL_BG.g, ui::PANEL_BG.b, 210});
        textCentered(hint, h.x + h.width / 2, h.y + 14, 16, ui::TEXT_DIM);
    }
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
        Rectangle r = centeredRect(ui::screenW() / 2, 128, w, 52);   // top centre, out of the way
        fillRound(r, 10, ui::PANEL_BG);
        textCentered(line1, r.x + r.width / 2, r.y + 15, 18, ui::TEXT, true);
        textCentered(line2, r.x + r.width / 2, r.y + 37, 16, ui::TEXT_DIM);
    }
    if (!bpListOpen_) return;   // (the save dialog is drawn by drawModals)
    Rectangle r = bpListRect();
    drawPanel(r, "Blueprints");
    Rectangle add{panelCloseRect(r).x - 10 - 90, r.y + 6, 90, 32};   // left of the X
    drawButton(add, "+ Add", ui::BTN_OK);

    drawButton({r.x + 12, r.y + 6, 190, 32}, "Add blueprint book", ui::BTN, 16);

    float top = r.y + LIST_TOP;
    Rectangle viewport{r.x, top, r.width, r.y + r.height - 8 - top};
    if (blueprints_.empty() && bpBooks_.empty()) {
        textCentered("No blueprints yet.", r.x + r.width / 2, top + 60, 20, BLACK);
        textCentered("Press '+ Add' or Ctrl + B and drag over your machines.", r.x + r.width / 2, top + 90, 16, ui::TEXT_DIM);
        return;
    }
    auto rows = bpListRows();
    float content = rows.empty() ? 0 : rows.back().y + rows.back().h;
    bpListScroll_ = clampScroll(bpListScroll_, content, viewport.height);
    bool dragging = bpDragging() && bpDragIndex_ < int(blueprints_.size());
    int dropBook = dragging ? bpDropBook(ui::mouse()) : -1;
    ui::beginScissor({viewport.x, viewport.y, viewport.width, viewport.height});
    for (const BpRow& row : rows) {
        Rectangle rr = bpRowRect(row);
        if (rr.y + rr.height < viewport.y || rr.y > viewport.y + viewport.height) continue;
        Rectangle del{rr.x + rr.width - 8 - 70, rr.y + (rr.height - 32) / 2, 70, 32};
        if (row.kind == BpRowKind::Book) {
            const BlueprintBook& bk = bpBooks_[size_t(row.index)];
            bool open = bpFold_.isOpen(bookKey(bk.id));
            FoldHeaderStyle st;
            st.bg = rgba(150, 147, 200, 170);
            Rectangle rename{del.x - 8 - 80, del.y, 80, 32};
            int n = int(std::count_if(blueprints_.begin(), blueprints_.end(), [&](const Blueprint& b) { return b.book == bk.id; }));
            std::string count = "(" + std::to_string(n) + ")";
            float room = rename.x - 10 - (rr.x + 34) - measure(" " + count, 16).x;
            float end = drawFoldHeader(rr, fitText(bk.name, room, 20), open, st);
            text(count, end + 8, rr.y + rr.height / 2 - 9, 16, ui::TEXT_DIM);
            drawButton(rename, "Rename", ui::BTN, 16);
            drawButton(del, "Delete", ui::BTN_DANGER, 16);
            if (dropBook == bk.id) lineRound(rr, 8, 3, WHITE);   // drop it here
        } else if (row.kind == BpRowKind::EmptyBook) {
            text("Empty - drag blueprints onto the book to put them in", rr.x + 10, rr.y + rr.height / 2 - 8, 15, ui::TEXT_DIM);
        } else {
            const Blueprint& bp = blueprints_[size_t(row.index)];
            bool held = dragging && row.index == bpDragIndex_;
            fillRound(rr, 8, held ? Color{255, 255, 255, 50} : ui::ROW_BG);
            float textW = del.x - 8 - 70 - 10 - (rr.x + 10);
            Color c = held ? ui::TEXT_DIM : BLACK;
            text(fitText(bp.name, textW, 20), rr.x + 10, rr.y + 4, 20, c, true);
            std::string info = std::to_string(bp.w) + "x" + std::to_string(bp.h) + ", " + objects(bp.count()) +
                               " - " + costText(bp.cost());
            text(fitText(info, textW, 14), rr.x + 10, rr.y + 28, 14, rgba(50, 50, 50));
            drawButton({del.x - 8 - 70, del.y, 70, 32}, "Use", ui::BTN);
            drawButton(del, "Delete", ui::BTN_DANGER);
        }
    }
    EndScissorMode();
    if (content > viewport.height)
        drawScrollbar({r.x + r.width - 8, viewport.y, 5, viewport.height}, bpListScroll_, content, viewport.height);
    if (dragging) {
        const Blueprint& bp = blueprints_[size_t(bpDragIndex_)];
        // dropping outside the books: the whole list lights up
        if (dropBook == 0 && bp.book != 0) lineRound(viewport, 8, 3, WHITE);
        Vector2 m = ui::mouse();
        std::string hint = dropBook > 0 ? "Put into this book" : dropBook == 0 ? (bp.book ? "Take out of the book" : bp.name) : bp.name;
        Vector2 sz = measure(hint, 16, true);
        Rectangle g{m.x + 14, m.y - 6, sz.x + 20, sz.y + 12};
        fillRound(g, 8, Color{60, 60, 120, 230});
        text(hint, g.x + 10, g.y + 6, 16, WHITE, true);
    }
}

// ---------------------------------------------------------------------------
// Blueprint books
// ---------------------------------------------------------------------------
void GameScreen::saveBlueprintList() { save::saveBlueprints(blueprints_, bpBooks_); }

// Books first (each followed by its blueprints when it is open), then the
// blueprints that are in no book.
std::vector<GameScreen::BpRow> GameScreen::bpListRows() const {
    std::vector<BpRow> rows;
    float y = 0;
    for (size_t b = 0; b < bpBooks_.size(); ++b) {
        int id = bpBooks_[b].id;
        rows.push_back({BpRowKind::Book, int(b), id, y, BOOK_H, 0});
        y += BOOK_H;
        if (!bpFold_.isOpen(bookKey(id))) continue;
        bool any = false;
        for (size_t i = 0; i < blueprints_.size(); ++i) {
            if (blueprints_[i].book != id) continue;
            rows.push_back({BpRowKind::Blueprint, int(i), id, y, ROW_H, BOOK_INDENT});
            y += ROW_H;
            any = true;
        }
        if (!any) {
            rows.push_back({BpRowKind::EmptyBook, -1, id, y, EMPTY_BOOK_H, BOOK_INDENT});
            y += EMPTY_BOOK_H;
        }
    }
    for (size_t i = 0; i < blueprints_.size(); ++i) {
        if (blueprints_[i].book != 0) continue;
        rows.push_back({BpRowKind::Blueprint, int(i), 0, y, ROW_H, 0});
        y += ROW_H;
    }
    return rows;
}

Rectangle GameScreen::bpRowRect(const BpRow& row) const {
    Rectangle r = bpListRect();
    float top = r.y + LIST_TOP + row.y - bpListScroll_;
    return {r.x + 14 + row.indent, top + 4, r.width - 28 - row.indent, row.h - 8};
}

const GameScreen::BpRow* GameScreen::bpRowAt(const std::vector<BpRow>& rows, Vector2 p) const {
    Rectangle r = bpListRect();
    Rectangle viewport{r.x, r.y + LIST_TOP, r.width, r.height - LIST_TOP - 8};
    if (!inRect(viewport, p)) return nullptr;
    for (const BpRow& row : rows) {
        Rectangle rr = bpRowRect(row);
        // the gaps between rows belong to the row above, so a drop never misses
        Rectangle hit{viewport.x, rr.y - 4, viewport.width, rr.height + 8};
        if (inRect(hit, p)) return &row;
    }
    return nullptr;
}

int GameScreen::bpDropBook(Vector2 p) const {
    Rectangle r = bpListRect();
    if (!inRect({r.x, r.y + LIST_TOP, r.width, r.height - LIST_TOP - 8}, p)) return -1;
    auto rows = bpListRows();
    const BpRow* row = bpRowAt(rows, p);
    return row ? row->book : 0;   // empty space: outside the books
}

bool GameScreen::bpDragging() const {
    if (bpDragIndex_ < 0) return false;
    Vector2 m = ui::mouse();
    float dx = m.x - bpDragFrom_.x, dy = m.y - bpDragFrom_.y;
    return dx * dx + dy * dy >= DRAG_MIN * DRAG_MIN;
}

Rectangle GameScreen::bpBookDialogRect() const {
    return centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::screenH() / 2) - 40, 400, 210);
}

void GameScreen::openBookDialog(int renameId) {
    bpBookRenameId_ = renameId;
    bpBookError_.clear();
    std::string name;
    if (renameId > 0) {
        for (auto& b : bpBooks_) if (b.id == renameId) name = b.name;
    } else {
        int n = 1;
        auto taken = [&](const std::string& nm) {
            return std::any_of(bpBooks_.begin(), bpBooks_.end(), [&](const BlueprintBook& b) { return b.name == nm; });
        };
        while (taken("Book " + std::to_string(n))) ++n;
        name = "Book " + std::to_string(n);
    }
    Rectangle r = bpBookDialogRect();
    bpBookNameInput_ = TextInput({r.x + 30, r.y + 76, r.width - 60, 40}, name);
    bpBookDialogOpen_ = true;
}

void GameScreen::submitBookDialog() {
    std::string name = bpBookNameInput_ ? bpBookNameInput_->text : "";
    size_t a = name.find_first_not_of(' ');
    if (a == std::string::npos) { bpBookError_ = "Please enter a name"; return; }
    name = name.substr(a, name.find_last_not_of(' ') - a + 1);
    for (auto& b : bpBooks_)
        if (b.name == name && b.id != bpBookRenameId_) { bpBookError_ = "A book with this name already exists"; return; }
    if (bpBookRenameId_ > 0) {
        for (auto& b : bpBooks_) if (b.id == bpBookRenameId_) b.name = name;
    } else {
        int id = 1;
        for (auto& b : bpBooks_) id = std::max(id, b.id + 1);
        bpBooks_.push_back({id, name});
        bpFold_.setOpen(bookKey(id), true);   // show it's empty and how to fill it
    }
    saveBlueprintList();
    bpBookDialogOpen_ = false;
    bpBookNameInput_.reset();
}

bool GameScreen::bpBookDialogHandle(const Event& e) {
    if (!bpBookDialogOpen_) return false;
    Rectangle r = bpBookDialogRect();
    Rectangle cancelBtn{r.x + 30, r.y + r.height - 58, 140, 40};
    Rectangle okBtn{r.x + r.width - 30 - 140, r.y + r.height - 58, 140, 40};
    auto cancel = [&]() { bpBookDialogOpen_ = false; bpBookNameInput_.reset(); };
    if (e.type == EvType::MouseDown && (!inRect(r, e.pos) || inRect(panelCloseRect(r), e.pos))) { cancel(); return true; }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(cancelBtn, e.pos)) { cancel(); return true; }
        if (inRect(okBtn, e.pos)) { submitBookDialog(); return true; }
    }
    if (bpBookNameInput_) {
        bpBookNameInput_->handle(e);
        if (bpBookNameInput_ && bpBookNameInput_->cancelled) { cancel(); return true; }
        if (bpBookNameInput_ && bpBookNameInput_->submitted) { bpBookNameInput_->submitted = false; submitBookDialog(); }
    }
    return true;   // modal
}

void GameScreen::drawBpBookDialog() {
    if (!bpBookDialogOpen_) return;
    Rectangle r = bpBookDialogRect();
    drawModalShade();
    drawPanel(r, bpBookRenameId_ > 0 ? "Rename book" : "New blueprint book");
    text("Name:", r.x + 30, r.y + 56, 14, ui::TEXT_DIM);
    if (bpBookNameInput_) {
        bpBookNameInput_->update(GetFrameTime());
        bpBookNameInput_->draw();
    }
    if (!bpBookError_.empty()) textCentered(bpBookError_, r.x + r.width / 2, r.y + 130, 15, rgba(170, 20, 20));
    drawButton({r.x + 30, r.y + r.height - 58, 140, 40}, "Cancel", ui::BTN_DANGER);
    drawButton({r.x + r.width - 30 - 140, r.y + r.height - 58, 140, 40}, bpBookRenameId_ > 0 ? "Rename" : "Create", ui::BTN_OK);
}
