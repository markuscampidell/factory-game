// game/GameScreen.cpp - lifecycle, fixed-tick loop, input routing and the
// build / delete / interaction logic.
#include "GameScreen.hpp"
#include "Assets.hpp"
#include <algorithm>


// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void View::follow(const RectF& r) {
    // Soft leash: once the player gets closer than `margin` screen pixels to
    // an edge, the camera eases after them. The margin is capped relative to
    // the window so it still leaves room to move on small/zoomed views.
    float mx = std::min(margin, w * 0.3f), my = std::min(margin, h * 0.3f);
    float px = (r.cx() - x) * zoom, py = (r.cy() - y) * zoom;
    if (px < mx) x -= (mx - px) * smooth / zoom;
    else if (px > w - mx) x += (px - (w - mx)) * smooth / zoom;
    if (py < my) y -= (my - py) * smooth / zoom;
    else if (py > h - my) y += (py - (h - my)) * smooth / zoom;
    keepVisible(r);
}

void View::keepVisible(const RectF& r) {
    // The soft leash alone can fall behind when zoomed in (the player then
    // moves many screen pixels per tick), so clamp: the player's whole rect
    // must stay at least `border` screen pixels away from every edge.
    auto axis = [](float& cam, float lo, float hi, float screen, float z) {
        float border = std::max(40.0f, screen * 0.15f);
        float s0 = (lo - cam) * z, s1 = (hi - cam) * z;   // player edges on screen
        if (s1 - s0 > screen - 2 * border) {             // bigger than the safe area: centre it
            cam = (lo + hi) / 2 - screen / (2 * z);
            return;
        }
        if (s0 < border) cam -= (border - s0) / z;
        else if (s1 > screen - border) cam += (s1 - (screen - border)) / z;
    };
    axis(x, r.left(), r.right(), w, zoom);
    axis(y, r.top(), r.bottom(), h, zoom);
}

void View::visibleTiles(int& l, int& t, int& r, int& b) const {
    l = int(std::floor(x / CELL_SIZE));
    t = int(std::floor(y / CELL_SIZE));
    r = int(std::floor((x + viewW()) / CELL_SIZE)) + 1;
    b = int(std::floor((y + viewH()) / CELL_SIZE)) + 1;
}

void View::zoomAt(float factor, Vector2 anchor) {
    Vector2 before = screenToWorld(anchor);
    zoom = std::clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM);
    x = before.x - anchor.x / zoom;
    y = before.y - anchor.y / zoom;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
GameScreen::GameScreen() {
    cam_.w = float(GetScreenWidth());
    cam_.h = float(GetScreenHeight());
    blueprints_ = save::loadBlueprints(&bpBooks_);
    builder_.requireExplored = true;   // no building in chunks you haven't seen yet
    builder_.blueprintReach = 4;       // blueprints / copies: 9 x 9 chunks around the player
    world_.recordChanges = true;       // for undo (Ctrl + Z)
}

GameScreen::~GameScreen() {
    for (auto& kv : mapTerrain_) UnloadTexture(kv.second);
}

// ---------------------------------------------------------------------------
// Zoom / map view
// ---------------------------------------------------------------------------
// Zooming is smooth: zoomBy / toggleMap only change targetZoom_, and
// stepZoom (once per frame) glides cam_.zoom towards it.

// M / the minimap / the zoom gate: glide into the map view around the player, or back
// to the zoom you had before.
void GameScreen::toggleMap() {
    bool toMap = targetZoom_ >= MAP_ZOOM;
    if (toMap) lastNormalZoom_ = targetZoom_;
    setZoomTarget(toMap ? 0.2f : std::max(lastNormalZoom_, MAP_ZOOM),
                  {ui::screenW() / 2, ui::screenH() / 2});
    zoomToPlayer_ = true;   // centre on the player while zooming
}

// `anchor` is in UI coordinates (mouse / event position)
// (mouse wheel, + / -). Zooming stops at the edge of the current view (40%
// for the normal view, MAP_TOP for the map). Pushing on at that edge fills
// the zoom gate; ZOOM_GATE_STEPS notches in a row switch views.
namespace { constexpr float MAP_TOP = MAP_ZOOM / 1.15f; }   // closest map zoom

bool GameScreen::zoomGateActive() const { return zoomGate_ > 0 && GetTime() - zoomGateLast_ <= ZOOM_GATE_TIME; }

void GameScreen::zoomBy(float factor, Vector2 anchor) {
    double now = GetTime();
    bool spinning = now - zoomLastNotch_ < ZOOM_SPIN_PAUSE;   // came right after the previous notch
    zoomLastNotch_ = now;
    bool inMap = targetZoom_ < MAP_ZOOM;
    int dir = inMap ? (factor > 1 ? -1 : 0) : (factor < 1 ? 1 : 0);   // pushing towards the other view?
    float lo = inMap ? MIN_ZOOM : MAP_ZOOM, hi = inMap ? MAP_TOP : MAX_ZOOM;
    float target = std::clamp(targetZoom_ * factor, lo, hi);
    bool atEdge = std::fabs(target - targetZoom_) < 1e-4f;
    if (dir != 0 && atEdge) {
        bool newRun = !zoomGateActive() || zoomGateDir_ != dir;
        if (newRun && spinning) {
            // still spinning from zooming up to the edge: stay at the edge
            // for ZOOM_EDGE_HOLD, then let the notches count
            if (zoomEdgeSince_ < 0) zoomEdgeSince_ = now;
            if (now - zoomEdgeSince_ < ZOOM_EDGE_HOLD) {
                zoomGate_ = 0;   // (no box yet - it appears once counting starts)
                zoomGateDir_ = dir;
                return;
            }
        }
        zoomEdgeSince_ = -1;
        if (newRun) zoomGate_ = 0;
        zoomGateDir_ = dir;
        zoomGateLast_ = now;
        if (++zoomGate_ >= ZOOM_GATE_STEPS) {
            zoomGate_ = 0;
            toggleMap();
        }
        return;
    }
    zoomGate_ = 0;   // zoomed normally / the other way: the run is over
    zoomEdgeSince_ = -1;
    if (!atEdge) setZoomTarget(target, anchor);
}

void GameScreen::setZoomTarget(float target, Vector2 anchor) {
    targetZoom_ = std::clamp(target, MIN_ZOOM, MAX_ZOOM);
    zoomAnchor_ = ui::toReal(anchor);   // the camera works in real window pixels
    zoomToPlayer_ = false;
    // back to the normal view: the camera glides back to the player, so new
    // chunks are only ever generated around where the player actually is
    if (mapMode() && targetZoom_ >= MAP_ZOOM) zoomToPlayer_ = true;
}

void GameScreen::stepZoom(float dt) {
    if (cam_.zoom == targetZoom_) { zoomToPlayer_ = false; return; }
    bool wasMap = mapMode();
    // ease out in log space (every doubling takes the same time); about
    // 90 % of the way after 0.15 s
    float k = 1.0f - std::exp(-dt * 15.0f);
    float ratio = targetZoom_ / cam_.zoom;
    float next = std::fabs(std::log(ratio)) < 0.002f ? targetZoom_ : cam_.zoom * std::pow(ratio, k);
    if (zoomToPlayer_) {
        // zoom around the screen centre and slide that centre to the player
        float cx = cam_.x + cam_.viewW() / 2, cy = cam_.y + cam_.viewH() / 2;
        cam_.zoom = next;
        float t = next == targetZoom_ ? 1.0f : k;
        cam_.centerOn(cx + (player_.rect.cx() - cx) * t, cy + (player_.rect.cy() - cy) * t);
    } else {
        cam_.zoomAt(next / cam_.zoom, zoomAnchor_);
    }
    if (!wasMap && mapMode()) enterMapMode();
    // zooming around the mouse must never push the player off screen
    if (!mapMode() && !zoomToPlayer_) cam_.keepVisible(player_.rect);
}

// Leaving the normal view: stop building / deleting single things. Copy,
// paste, area delete and blueprints (bpMode_) keep going - they work in the
// map view too. Nothing is half done at this point - every placement /
// deletion is all-or-nothing and only happens on a click, so no items can be
// lost by switching.
void GameScreen::enterMapMode() {
    if (bpMode_ == BpMode::None) mode_ = BuildMode::None;
    placingBelt_ = false;
    lastDragged_.reset();
    dragPlaced_.clear();
    hovered_ = hoveredDelete_ = 0;
    // panels about things in the world close (a machine / storage panel
    // together with its inventory); inventory / crafting / blueprints stay
    if (machinePanel_ >= 0 || storagePanel_ >= 0 || beltPanel_ >= 0 || splitterPanel_ >= 0) closeAllPanels();
    // the blueprint list and the Save blueprint dialog stay open
}

void GameScreen::newGame(const std::string& name) {
    CameraState cs;
    save::newGame(world_, player_, cs, name);
    world_.takeRecord();   // building the world isn't something to undo
    undoStack_.clear();
    redoStack_.clear();
    markers_.clear();
    saveName_ = name;
    playSeconds_ = 0;
    cam_.centerOn(player_.rect.cx(), player_.rect.cy());
    targetZoom_ = cam_.zoom;
}

bool GameScreen::loadGame(const std::string& name, std::string* error, bool fromAutosave) {
    CameraState cs;
    if (!save::loadGame(world_, player_, cs, fromAutosave ? save::autosaveName(name) : name, error)) return false;
    if (fromAutosave) save::setLastOpened(name);   // Continue keeps meaning "name"
    world_.takeRecord();   // loading isn't something to undo
    undoStack_.clear();
    redoStack_.clear();
    markers_ = cs.markers;
    playSeconds_ = cs.playSeconds;
    saveName_ = name;
    cam_.zoom = targetZoom_ = std::clamp(cs.zoom, MIN_ZOOM, MAX_ZOOM);
    zoomToPlayer_ = false;
    showOutputs_ = cs.showOutputs;
    if (cs.valid) { cam_.x = cs.x; cam_.y = cs.y; }
    else cam_.centerOn(player_.rect.cx(), player_.rect.cy());
    return true;
}

void GameScreen::saveNow(const std::string& name) {
    CameraState cs{cam_.x, cam_.y, true, cam_.zoom, showOutputs_, markers_, playSeconds_};
    std::string err;
    if (!save::saveGame(world_, player_, cs, name, &err)) notify("Saving failed: " + err);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void GameScreen::update(float dt) {
    if (altClean_ && !IsWindowFocused()) altClean_ = false;
    if (altClean_ && (IsKeyReleased(KEY_LEFT_ALT) || IsKeyReleased(KEY_RIGHT_ALT))) {
        altClean_ = false;
        showOutputs_ = !showOutputs_;
    }
    cam_.w = float(GetScreenWidth());
    cam_.h = float(GetScreenHeight());
    fps_ = float(GetFPS());
    if (toastTime_ > 0) toastTime_ -= dt;

    if (!menuOpen_ && !controlsOpen_) {   // the game pauses behind these
        const double step = 1.0 / TICKS_PER_SECOND;
        accumulator_ += std::min(double(dt), 0.25);
        int steps = 0;
        while (accumulator_ >= step && steps < 10) {
            tick();
            accumulator_ -= step;
            ++steps;
        }
        if (steps == 10) accumulator_ = 0; // can't keep up: slow down rather than spiral
        interp_ = float(accumulator_ / step);
        // play time + autosave (game time; paused while the Game Menu / Controls are open)
        playSeconds_ += std::min(double(dt), 0.25);
        autosaveTimer_ += dt;
        if (autosaveTimer_ >= AUTOSAVE_SECONDS) {
            autosaveTimer_ = 0;
            saveNow(autosaveName());
            notify("Autosaved ('" + autosaveName() + "')");
        }
        stepZoom(dt);
        perFrame();
    }
}

void GameScreen::tick() {
    int ix = 0, iy = 0;
    // no walking while typing (search box, a name in a dialog)
    bool typing = focusedSearch() != nullptr || anyModalOpen();
    if (!typing) {
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) ix += 1;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) ix -= 1;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) iy += 1;
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) iy -= 1;
    }
    if (mapMode()) {
        // map view: the player stands still, the keys pan the map instead
        player_.update(world_, 0, 0);
        cam_.x += float(ix) * 12.0f / cam_.zoom;
        cam_.y += float(iy) * 12.0f / cam_.zoom;
    } else {
        player_.update(world_, ix, iy);
        cam_.follow(player_.rect);
    }
    if (craftOpen_) updateHandCrafting(player_);
    updateHandMining();
    world_.tick();
}

// ---------------------------------------------------------------------------
// Hand mining: hold the right mouse button on an ore tile. One ore takes
// HAND_MINE_TICKS (twice as long as a miner). If the inventory is full the
// ore stays in the ground and a message appears next to the tile.
// ---------------------------------------------------------------------------
void GameScreen::updateHandMining() {
    if (fullMessageCooldown_ > 0) --fullMessageCooldown_;
    Vector2 m = ui::mouse();
    bool holding = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !shiftDown() && !mapMode() &&
                   mode_ == BuildMode::None && !mouseOverOpenPanel(m) && !overHudButtons(m);
    Vec2i tile = screenToTile(m);
    ItemId ore = holding && world_.refAt(tile) == 0 ? world_.oreAt(tile) : NO_ITEM;
    if (ore == NO_ITEM) {
        mining_ = false;
        miningTicks_ = 0;
        return;
    }
    if (!mining_ || tile != miningTile_) {
        mining_ = true;
        miningTile_ = tile;
        miningTicks_ = 0;
    }
    if (miningTicks_ < HAND_MINE_TICKS) ++miningTicks_;
    if (miningTicks_ >= HAND_MINE_TICKS) {
        float wx = float(tile.x * CELL_SIZE + CELL_SIZE / 2), wy = float(tile.y * CELL_SIZE);
        if (player_.inventory.tryAdd(ore, 1)) {
            miningTicks_ = 0;
            floatText(wx, wy, "+1 " + itemDef(ore).name, Color{255, 255, 255, 255});
        } else if (fullMessageCooldown_ == 0) {
            // keep the progress full; it finishes as soon as there's room
            floatText(wx, wy, "Inventory full", Color{255, 90, 90, 255});
            fullMessageCooldown_ = TICKS_PER_SECOND;
        }
    }
}

// ---------------------------------------------------------------------------
// Copy / paste: Shift + Right Click copies a machine's recipe, a belt's
// filter or a splitter's three filters. Shift + Left Click pastes; keep it
// held and sweep the mouse to paste onto many things quickly.
// ---------------------------------------------------------------------------
bool GameScreen::copyPasteAllowed(Vector2 p) const {
    return !mapMode() && mode_ == BuildMode::None && !mouseOverOpenPanel(p);
}

bool GameScreen::copyPasteClick(const Event& e) {
    if (e.type != EvType::MouseDown || !shiftDown() || !copyPasteAllowed(e.pos)) return false;
    if (e.button == MB_RIGHT) { copyAt(e.pos); return true; }
    if (e.button == MB_LEFT) {
        pasteHolding_ = true;
        pastedThisHold_.clear();
        pasteAt(e.pos);
        return true;
    }
    return false;
}

void GameScreen::updatePasteHold() {
    if (!pasteHolding_) return;
    Vector2 m = ui::mouse();
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) || !shiftDown()) { pasteHolding_ = false; return; }
    if (copyPasteAllowed(m)) pasteAt(m);
}

void GameScreen::copyAt(Vector2 p) {
    TileRef r = world_.refAt(screenToTile(p));
    const Color ok{120, 255, 140, 255}, bad{255, 110, 110, 255};
    if (r > 0) {
        const Belt& b = world_.belt(r - 1);
        copiedBeltFilter_ = b.filter;
        floatText(float(b.pos.x * CELL_SIZE + CELL_SIZE / 2), float(b.pos.y * CELL_SIZE), "Copied settings: belt filter", ok);
    } else if (r < 0) {
        Machine& m = world_.machine(-r - 1);
        RectF rc = m.pixelRect();
        if (m.isSplitter()) {
            copiedSplitterFilters_ = m.sideFilters;
            floatText(rc.cx(), rc.top(), "Copied settings: splitter filters", ok);
        } else if (m.recipeChoices() && m.recipe) {
            copiedRecipe_ = m.recipe;
            floatText(rc.cx(), rc.top(), "Copied settings: recipe " + m.recipe->name, ok);
        } else {
            floatText(rc.cx(), rc.top(), "No settings to copy here", bad);
        }
    }
}

// Every pasted object gets its own message that floats up and fades out.
void GameScreen::pasteSuccess(float wx, float wy, const std::string& /*kind*/, const std::string& what) {
    floatText(wx, wy, "Pasted settings: " + what, Color{120, 255, 140, 255});
}

void GameScreen::pasteAt(Vector2 p) {
    TileRef r = world_.refAt(screenToTile(p));
    if (r == 0 || !pastedThisHold_.insert(r).second) return;   // each object once per hold
    const Color bad{255, 110, 110, 255};

    if (r > 0) {   // belt
        Belt& b = world_.belt(r - 1);
        float wx = float(b.pos.x * CELL_SIZE + CELL_SIZE / 2), wy = float(b.pos.y * CELL_SIZE);
        if (!copiedBeltFilter_) { floatText(wx, wy, "No belt settings copied (Shift + Right Click a belt)", bad); return; }
        b.filter = *copiedBeltFilter_;
        pasteSuccess(wx, wy, "belt", "belt filter");
        return;
    }
    Machine& m = world_.machine(-r - 1);
    RectF rc = m.pixelRect();
    float wx = rc.cx(), wy = rc.top();
    if (m.isSplitter()) {
        if (!copiedSplitterFilters_) { floatText(wx, wy, "No splitter settings copied (Shift + Right Click a splitter)", bad); return; }
        m.sideFilters = *copiedSplitterFilters_;
        pasteSuccess(wx, wy, "splitter", "splitter filters");
        return;
    }
    if (!copiedRecipe_) { floatText(wx, wy, "No machine settings copied (Shift + Right Click a machine)", bad); return; }
    const auto* choices = m.recipeChoices();
    if (!choices || std::find(choices->begin(), choices->end(), copiedRecipe_) == choices->end()) {
        floatText(wx, wy, std::string(machineSpec(m.type).name) + " can't use the " + copiedRecipe_->name + " recipe", bad);
        return;
    }
    if (m.recipe == copiedRecipe_ || builder_.setRecipe(m, copiedRecipe_))
        pasteSuccess(wx, wy, "recipe", "recipe: " + copiedRecipe_->name);
    else
        floatText(wx, wy, "Not enough inventory space for the machine's items", bad);
}

void GameScreen::showItemDelta(float wx, float wy, const ItemAmounts& delta) {
    std::string s = itemDeltaText(delta);
    if (s.empty()) return;   // e.g. a belt replaced by the same belt
    bool gained = false, spent = false;
    for (auto& d : delta) {
        gained |= d.second > 0;
        spent |= d.second < 0;
    }
    Color c = !spent ? Color{120, 255, 140, 255} : !gained ? Color{255, 200, 90, 255} : Color{255, 255, 255, 255};
    floatText(wx, wy, s, c);
}

void GameScreen::floatText(float wx, float wy, const std::string& s, Color c) {
    // replace an identical message at the same spot instead of stacking them
    for (auto& f : floatTexts_)
        if (f.text == s && std::fabs(f.wx - wx) < 1 && std::fabs(f.wy - wy) < 40) { f.age = 0; f.wy = wy; return; }
    floatTexts_.push_back({wx, wy, s, c, 0});
}

void GameScreen::perFrame() {
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) commitUndo();   // a whole drag = one undo step
    validatePanels();
    updatePasteHold();

    updateDrag();

    // panels close themselves once their object scrolls off screen
    auto offscreen = [&](const Machine& mm) {
        RectF r = mm.pixelRect();
        return r.right() - cam_.x < 0 || r.left() - cam_.x > cam_.viewW() || r.bottom() - cam_.y < 0 || r.top() - cam_.y > cam_.viewH();
    };
    // (a machine / storage panel closes together with its inventory)
    if (machinePanel_ >= 0 && offscreen(world_.machine(machinePanel_))) closeAllPanels();
    if (storagePanel_ >= 0 && offscreen(world_.machine(storagePanel_))) closeAllPanels();
    if (splitterPanel_ >= 0 && offscreen(world_.machine(splitterPanel_))) { splitterPanel_ = -1; picker_.reset(); }
    if (beltPanel_ >= 0) {
        Vec2i p = world_.belt(beltPanel_).pos;
        float sx = float(p.x * CELL_SIZE) - cam_.x, sy = float(p.y * CELL_SIZE) - cam_.y;
        if (sx + CELL_SIZE < 0 || sx > cam_.viewW() || sy + CELL_SIZE < 0 || sy > cam_.viewH()) { beltPanel_ = -1; picker_.reset(); }
    }
}

// Panels refer to objects by id; make sure the object still exists.
void GameScreen::validatePanels() {
    auto check = [&](int& id, Vec2i pos) {
        if (id >= 0 && (!world_.machineAlive(id) || world_.machine(id).pos != pos)) id = -1;
    };
    bool hadPair = machinePanel_ >= 0 || storagePanel_ >= 0;
    check(machinePanel_, machinePanelPos_);
    check(storagePanel_, storagePanelPos_);
    if (hadPair && machinePanel_ < 0 && storagePanel_ < 0) invOpen_ = false;   // its machine is gone
    int before = splitterPanel_;
    check(splitterPanel_, splitterPanelPos_);
    if (beltPanel_ >= 0 && (beltPanel_ >= int(world_.belts().size()) || !world_.belt(beltPanel_).alive ||
                            world_.belt(beltPanel_).pos != beltPanelPos_))
        beltPanel_ = -1;
    if (picker_ && ((picker_->splitter && splitterPanel_ < 0) || (!picker_->splitter && beltPanel_ < 0))) picker_.reset();
    (void)before;
}

// ---------------------------------------------------------------------------
// Event routing (same order as GameOrchestrator._handle_event)
// ---------------------------------------------------------------------------
void GameScreen::handle(const Event& e) {
    if (e.type == EvType::MouseUp && e.button == MB_LEFT) justPlaced_ = false;
    // a search box with the focus gets every key; a click elsewhere drops it
    if (SearchBox* sb = focusedSearch(); sb && (e.type == EvType::KeyDown || e.type == EvType::Text)) {
        sb->handle(e);
        return;
    }
    if (e.type == EvType::MouseDown)
        for (SearchBox* sb : {&craftSearch_, &pickerSearch_, &bpSearch_, &machineSearch_})
            if (sb->focused() && !inRect(sb->rect, e.pos)) sb->unfocus();
    // Alt toggles the machine outputs when it's released on its own
    // (so Alt+Tab or Alt+something else doesn't toggle it)
    if (e.type == EvType::KeyDown) altClean_ = e.key == KEY_LEFT_ALT || e.key == KEY_RIGHT_ALT;
    if (e.type == EvType::MouseDown) altClean_ = false;

    if (menuBarHandle(e)) return;
    if (blueprintHandle(e)) return;
    if (hotbarHandle(e)) return;
    if (copyPasteClick(e)) return;
    if (pipetteClick(e)) return;
    if (panelCloseClick(e)) return;

    if (e.type == EvType::KeyDown && e.key == KEY_ESCAPE && mode_ == BuildMode::None && !anyPanelOpen()) {
        menuOpen_ = true;
        return;
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && mode_ == BuildMode::None && !justPlaced_)
        closeIfClickedOutside(e.pos);

    handleKeys(e);
    handleMouse(e);
    handlePlacement(e);

    machinePanelHandle(e);
    storagePanelHandle(e);
    beltFilterHandle(e);
    splitterFilterHandle(e);
    inventoryHandle(e);

    worldClick(e);
    pickKey(e);
}

void GameScreen::handleKeys(const Event& e) {
    if (e.type != EvType::KeyDown) return;
    if (e.key == KEY_ESCAPE) {   // first the panels, one by one, then build mode
        if (!closeTopPanel()) resetBuildState();
        return;
    }
    if (e.key == KEY_EQUAL || e.key == KEY_KP_ADD) { zoomBy(1.25f, {ui::screenW() / 2, ui::screenH() / 2}); return; }
    if (e.key == KEY_MINUS || e.key == KEY_KP_SUBTRACT) { zoomBy(1 / 1.25f, {ui::screenW() / 2, ui::screenH() / 2}); return; }
    if (e.key == KEY_M) { toggleMap(); return; }
    if (e.key == KEY_X) {
        closeAllPanels();
        if (mapMode()) notify("Zoom in to build or delete");
        else toggleDelete();
        return;
    }
    if (e.key == KEY_F) { toggleCrafting(); return; }
    if (e.key == KEY_TAB) { toggleInventory(); return; }
    if (craftOpen_) {
        if (e.key == KEY_SPACE) {
            player_.craftMode = player_.craftMode == CraftMode::Auto ? CraftMode::None : CraftMode::Auto;
            player_.craftTicks = 0;
        }
        return;
    }
    if (invOpen_ || machinePanel_ >= 0 || storagePanel_ >= 0 || beltPanel_ >= 0 || splitterPanel_ >= 0) return;
    if (e.key == KEY_R && mode_ == BuildMode::Building) { rotateSelected(shiftDown() ? -1 : 1); return; }
    if (e.key == KEY_T && mode_ == BuildMode::Building) { rotateSelected(2); return; }
}

void GameScreen::handleMouse(const Event& e) {
    if (e.type == EvType::Wheel) {
        if (craftOpen_) {
            Rectangle viewport = craftListViewport();
            if (inRect(viewport, e.pos)) {
                float content = float(craftVisibleRecipes().size()) * 45;
                craftScroll_ = clampScroll(craftScroll_ - e.wheel * 40, content, viewport.height);
                return;
            }
        }
        // anywhere else (not over a panel): zoom around the mouse
        if (!mouseOverOpenPanel(e.pos)) zoomBy(std::pow(1.15f, e.wheel), e.pos);
        return;
    }
    if (e.type != EvType::MouseDown) return;
    if (e.button == MB_RIGHT) { cancelBuildOrDelete(); return; }
    if (e.button == MB_LEFT && craftOpen_) craftHandleClick(e);
}

// TAB / the Inventory button on the left edge
void GameScreen::toggleInventory() {
    if (invOpen_) {
        closePanel(Panel::Inventory);   // together with its partner
    } else {
        resetBuildState();
        makeRoomFor(Panel::Inventory);
        invOpen_ = true;
    }
}

// F / the Handcrafting button on the right edge
void GameScreen::toggleCrafting() {
    if (!craftOpen_) {
        resetBuildState();
        makeRoomFor(Panel::Craft);
        craftOpen_ = true;
        invOpen_ = true;
    } else {
        closePanel(Panel::Craft);   // together with the inventory
    }
}

// ---------------------------------------------------------------------------
// Build state
// ---------------------------------------------------------------------------
void GameScreen::resetBuildState() {
    markerPlacing_ = false;
    bpTargetBook_ = 0;
    bpConfigIndex_ = -1;
    bpPendingName_.clear();
    bpMode_ = BpMode::None;
    bpDragging_ = false;
    mode_ = BuildMode::None;
    selected_ = Buildable::Smelter;
    placingBelt_ = false;
    resetRotation();
}

void GameScreen::select(Buildable b) {
    markerPlacing_ = false;
    if (mapMode()) { notify("Zoom in to build"); return; }
    bpMode_ = BpMode::None;
    bpDragging_ = false;
    selected_ = b;
    splitterRot_ = 0;
    placingBelt_ = false;
    mode_ = BuildMode::Building;
}

void GameScreen::toggleDelete() {
    markerPlacing_ = false;
    bpMode_ = BpMode::None;
    bpDragging_ = false;
    if (mapMode()) { mode_ = BuildMode::None; notify("Zoom in to build or delete"); return; }
    mode_ = mode_ == BuildMode::Deleting ? BuildMode::None : BuildMode::Deleting;
}

void GameScreen::enterBuild() {
    if (mapMode()) { mode_ = BuildMode::None; return; }
    mode_ = BuildMode::Building;
    placingBelt_ = false;
}

void GameScreen::rotateSelected(int steps) {
    if (selected_ == Buildable::Splitter) splitterRot_ = floorMod(splitterRot_ + steps, 4);
    else if (selected_ != Buildable::Belt && machineTurnable(toMachine(selected_)))
        splitterRot_ = floorMod(splitterRot_ + steps, 2);   // standing (0) / lying (1); T changes nothing
    else if (selected_ == Buildable::Belt) beltDir_ = rotateCW(beltDir_, steps);
}

void GameScreen::cancelBuildOrDelete() {
    if (placingBelt_) { placingBelt_ = false; return; }
    if (mode_ == BuildMode::Building) { resetBuildState(); return; }
    if (mode_ == BuildMode::Deleting) { enterBuild(); return; }
}

Vec2i GameScreen::screenToTile(Vector2 p) const {
    Vector2 w = cam_.screenToWorld(ui::toReal(p));   // p is in UI coordinates
    return {int(std::floor(w.x / CELL_SIZE)), int(std::floor(w.y / CELL_SIZE))};
}

Vec2i GameScreen::mouseTile() const { return screenToTile(ui::mouse()); }

// ---------------------------------------------------------------------------
// Placing & deleting
// ---------------------------------------------------------------------------
void GameScreen::handlePlacement(const Event& e) {
    if (mapMode() || bpMode_ != BpMode::None) return;
    if (invOpen_ || machinePanel_ >= 0 || storagePanel_ >= 0 || beltPanel_ >= 0 || splitterPanel_ >= 0) return;
    if (e.type == EvType::MouseUp && e.button == MB_LEFT) {
        lastDragged_.reset();
        dragPlaced_.clear();
        return;
    }
    if (!(e.type == EvType::MouseDown && e.button == MB_LEFT)) return;
    Vec2i tile = screenToTile(e.pos);

    if (mode_ == BuildMode::Deleting) { tryDeleteAt(tile); return; }

    if (mode_ == BuildMode::Building && selected_ == Buildable::Belt) {
        if (mouseOverOpenPanel(e.pos)) return;
        if (!placingBelt_) {
            placingBelt_ = true;
            beltStart_ = tile;
        } else {
            ItemAmounts delta;
            if (builder_.placeBelts(beltStart_, tile, beltDir_, shiftDown(), &delta))
                showItemDelta(float(tile.x * CELL_SIZE + CELL_SIZE / 2), float(tile.y * CELL_SIZE), delta);
            placingBelt_ = false;
        }
        return;
    }
    if (mode_ == BuildMode::Building) tryPlaceMachineAt(tile);
}

void GameScreen::updateDrag() {
    if (mapMode() || bpMode_ != BpMode::None) return;
    if (invOpen_ || machinePanel_ >= 0 || storagePanel_ >= 0 || beltPanel_ >= 0 || splitterPanel_ >= 0) return;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
    if (mode_ == BuildMode::Building && selected_ == Buildable::Belt) return;
    if (overHudButtons(ui::mouse())) return;
    // don't build/delete through the hotbar or the menu button
    for (auto& r : hotbarRects()) if (inRect(r, ui::mouse())) return;
    Vec2i tile = mouseTile();
    if (mode_ == BuildMode::Deleting) tryDeleteAt(tile);
    else if (mode_ == BuildMode::Building) tryPlaceMachineAt(tile);
}

void GameScreen::tryDeleteAt(Vec2i tile) {
    if (mapMode()) return;
    if (lastDragged_ && *lastDragged_ == tile) return;
    // the message appears above the deleted object
    float wx = float(tile.x * CELL_SIZE + CELL_SIZE / 2), wy = float(tile.y * CELL_SIZE);
    if (int mid = world_.machineIdAt(tile); mid >= 0) {
        RectF rc = world_.machine(mid).pixelRect();
        wx = rc.cx();
        wy = rc.top();
    }
    ItemAmounts got;
    bool deleted = builder_.deleteMachineAt(tile, &got);
    if (!deleted) deleted = builder_.deleteBeltAt(tile, shiftDown(), &got);
    if (deleted) {
        lastDragged_ = tile;
        showItemDelta(wx, wy, got);
    }
}

void GameScreen::tryPlaceMachineAt(Vec2i tile) {
    if (mapMode()) return;
    if (lastDragged_ && *lastDragged_ == tile) return;
    ItemAmounts delta;
    int id = builder_.placeMachine(toMachine(selected_), tile, Dir(splitterRot_), shiftDown(), &dragPlaced_, &delta);
    if (id >= 0) {
        lastDragged_ = tile;
        dragPlaced_.insert(id);
        justPlaced_ = true;
        RectF rc = world_.machine(id).pixelRect();
        showItemDelta(rc.cx(), rc.top(), delta);
    }
}

// ---------------------------------------------------------------------------
// Middle click: pick up what's under the mouse (type and direction only -
// recipe and filters are left at their defaults) and build copies of it.
// ---------------------------------------------------------------------------
bool GameScreen::pipetteClick(const Event& e) {
    if ((e.type != EvType::MouseDown && e.type != EvType::MouseUp) || e.button != MB_MIDDLE) return false;
    // middle clicks are only for this, never for panels or the world
    if (e.type == EvType::MouseUp || mapMode() || overUi(e.pos)) return true;
    TileRef r = objectAt(e.pos);
    if (r == 0) return true;
    closeAllPanels();
    float wx, wy;
    std::string name;
    if (r > 0) {
        const Belt& b = world_.belt(r - 1);
        select(Buildable::Belt);
        beltDir_ = b.dir;
        wx = float(b.pos.x * CELL_SIZE + CELL_SIZE / 2);
        wy = float(b.pos.y * CELL_SIZE);
        name = "belt";
    } else {
        const Machine& m = world_.machine(-r - 1);
        select(toBuildable(m.type));
        if (m.isSplitter() || machineTurnable(m.type)) splitterRot_ = int(m.dir) % (m.isSplitter() ? 4 : 2);
        RectF rc = m.pixelRect();
        wx = rc.cx();
        wy = rc.top();
        name = machineSpec(m.type).name;
    }
    if (mode_ != BuildMode::Building) return true;   // select() refused
    floatText(wx, wy, "Picked " + name, Color{120, 255, 140, 255});
    return true;
}

// ---------------------------------------------------------------------------
// Clicking things in the world
// ---------------------------------------------------------------------------
bool GameScreen::mouseOverOpenPanel(Vector2 p) const {
    if (invOpen_ && inRect(inventoryRect(), p)) return true;
    if (craftOpen_ && inRect(craftRect(), p)) return true;
    if (machinePanel_ >= 0 && inRect(machineRect(), p)) return true;
    if (storagePanel_ >= 0 && inRect(storageRect(), p)) return true;
    if (beltPanel_ >= 0 && inRect(beltFilterRect(), p)) return true;
    if (splitterPanel_ >= 0 && inRect(splitterFilterRect(), p)) return true;
    if (bpListOpen_ && inRect(bpListRect(), p)) return true;
    if (bpSaveOpen_ && inRect(bpSaveRect(), p)) return true;
    return false;
}

bool GameScreen::interactionAllowed(Vector2 p) const {
    if (mode_ != BuildMode::None || mapMode()) return false;
    return !mouseOverOpenPanel(p);
}

TileRef GameScreen::objectAt(Vector2 p) const { return world_.refAt(screenToTile(p)); }

void GameScreen::worldClick(const Event& e) {
    if (e.type != EvType::MouseDown || e.button != MB_LEFT || justPlaced_ || shiftDown()) return;
    if (!interactionAllowed(e.pos)) return;
    TileRef r = objectAt(e.pos);
    if (r < 0) {
        int id = -r - 1;
        Machine& m = world_.machine(id);
        if (m.isProducing()) {
            makeRoomFor(Panel::Machine);
            machinePanel_ = id;
            machinePanelPos_ = m.pos;
            invOpen_ = true;
        } else if (m.isStorage()) {
            makeRoomFor(Panel::Storage);
            storagePanel_ = id;
            storagePanelPos_ = m.pos;
            invOpen_ = true;
        } else if (m.isSplitter()) {
            makeRoomFor(Panel::Splitter);
            splitterPanel_ = id;
            splitterPanelPos_ = m.pos;
        }
    } else if (r > 0) {
        makeRoomFor(Panel::Belt);
        beltPanel_ = r - 1;
        beltPanelPos_ = world_.belt(r - 1).pos;
    }
}

void GameScreen::pickKey(const Event& e) {
    if (e.type != EvType::KeyDown || e.key != KEY_C || ctrlDown()) return;
    Vector2 m = ui::mouse();
    if (!interactionAllowed(m)) return;
    TileRef r = objectAt(m);
    if (r == 0) return;
    closeAllPanels();
    if (r > 0) select(Buildable::Belt);
    else {
        switch (world_.machine(-r - 1).type) {
            case MachineType::Smelter: select(Buildable::Smelter); break;
            case MachineType::Assembler: select(Buildable::Assembler); break;
            case MachineType::Splitter: select(Buildable::Splitter); break;
            case MachineType::Storage: select(Buildable::Storage); break;
            case MachineType::Miner: select(Buildable::Miner); break;
            default: break;
        }
    }
    resetRotation();
}

// ---------------------------------------------------------------------------
// Panels: open / close
// ---------------------------------------------------------------------------
bool GameScreen::isOpen(Panel p) const {
    switch (p) {
        case Panel::Inventory: return invOpen_;
        case Panel::Craft: return craftOpen_;
        case Panel::Machine: return machinePanel_ >= 0;
        case Panel::Storage: return storagePanel_ >= 0;
        case Panel::Belt: return beltPanel_ >= 0;
        case Panel::Splitter: return splitterPanel_ >= 0;
        case Panel::Blueprints: return bpListOpen_;
    }
    return false;
}

Rectangle GameScreen::panelRect(Panel p) const {
    switch (p) {
        case Panel::Inventory: return inventoryRect();
        case Panel::Craft: return craftRect();
        case Panel::Machine: return machineRect();
        case Panel::Storage: return storageRect();
        case Panel::Belt: return beltFilterRect();
        case Panel::Splitter: return splitterFilterRect();
        case Panel::Blueprints: return bpListRect();
    }
    return {};
}

// A pair (inventory + machine / storage / crafting) always closes together,
// so closing any panel simply closes everything that is open.
void GameScreen::closePanel(Panel /*p*/) { closeAllPanels(); }

// Opening a panel replaces whatever is open (see the rules in GameScreen.hpp).
void GameScreen::makeRoomFor(Panel /*p*/) { closeAllPanels(); }

bool GameScreen::anyPanelOpen() const {
    return invOpen_ || craftOpen_ || machinePanel_ >= 0 || storagePanel_ >= 0 || beltPanel_ >= 0 || splitterPanel_ >= 0 || bpListOpen_ || bpSaveOpen_;
}

void GameScreen::closeAllPanels() {
    invOpen_ = false;
    if (craftOpen_) { player_.craftMode = CraftMode::None; player_.craftTicks = 0; }
    craftOpen_ = false;
    machinePanel_ = storagePanel_ = beltPanel_ = splitterPanel_ = -1;
    picker_.reset();
    bpListOpen_ = false;
    bpDeleteConfirm_.reset();
    bpDragIndex_ = -1;
    craftSearch_.clear();
    pickerSearch_.clear();
    bpSearch_.clear();
    machineSearch_.clear();
    bpBookDialogOpen_ = false;
    bpBookNameInput_.reset();
}

// ESC closes one thing at a time, topmost first. A machine / storage panel
// closes together with the inventory next to it (same as its X button).
bool GameScreen::closeTopPanel() {
    if (picker_) { picker_.reset(); return true; }   // the item picker is its own step
    if (!anyPanelOpen()) return false;
    closeAllPanels();   // a pair closes together
    return true;
}

// The red X in the top-right corner of every in-game panel. Checked
// topmost panel first (same order as they are drawn, reversed).
bool GameScreen::panelCloseClick(const Event& e) {
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return false;
    // (the blueprint list handles its own X in blueprintHandle)
    const Panel order[] = {Panel::Splitter, Panel::Belt, Panel::Storage, Panel::Machine, Panel::Craft, Panel::Inventory};
    for (Panel p : order) {
        if (!isOpen(p) || !inRect(panelCloseRect(panelRect(p)), e.pos)) continue;
        closePanel(p);   // closes the pair
        return true;
    }
    return false;
}

void GameScreen::closeIfClickedOutside(Vector2 p) {
    if (!anyPanelOpen()) return;
    if (mouseOverOpenPanel(p)) return;
    if (interactionAllowed(p) && !shiftDown() && objectAt(p) != 0) return;   // worldClick swaps the panel
    closeAllPanels();
}

// ---------------------------------------------------------------------------
// Hotbar (bottom of the screen)
// ---------------------------------------------------------------------------
namespace {
struct HotbarSlot { int key; int kind; Buildable b; }; // kind 0 machine, 1 none, 2 delete
const HotbarSlot HOTBAR[] = {
    {KEY_ONE, 0, Buildable::Smelter},  {KEY_TWO, 0, Buildable::Assembler}, {KEY_THREE, 0, Buildable::Belt},
    {KEY_FOUR, 0, Buildable::Splitter}, {KEY_FIVE, 0, Buildable::Storage}, {KEY_SIX, 0, Buildable::Miner},
    {KEY_SEVEN, 1, Buildable::Smelter}, {KEY_EIGHT, 2, Buildable::Smelter},
};
constexpr int HOTBAR_N = 8;
constexpr float SLOT = 56, GAP = 8, MARGIN_BOTTOM = 18;
} // namespace

std::vector<Rectangle> GameScreen::hotbarRects() const {
    std::vector<Rectangle> rs;
    float w = ui::screenW(), h = ui::screenH();
    float total = HOTBAR_N * SLOT + (HOTBAR_N - 1) * GAP;
    float sx = std::floor(w / 2 - total / 2), y = h - SLOT - MARGIN_BOTTOM;
    for (int i = 0; i < HOTBAR_N; ++i) rs.push_back({sx + i * (SLOT + GAP), y, SLOT, SLOT});
    return rs;
}

bool GameScreen::hotbarHandle(const Event& e) {
    if (e.type == EvType::KeyDown) {
        int kp[] = {KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8};
        for (int i = 0; i < HOTBAR_N; ++i)
            if (e.key == HOTBAR[i].key || e.key == kp[i]) { hotbarActivate(i); return true; }
        return false;
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        auto rs = hotbarRects();
        for (int i = 0; i < HOTBAR_N; ++i)
            if (inRect(rs[size_t(i)], e.pos)) { hotbarActivate(i); return true; }
    }
    return false;
}

void GameScreen::hotbarActivate(int i) {
    closeAllPanels();
    const HotbarSlot& s = HOTBAR[i];
    if (s.kind == 1) resetBuildState();
    else if (s.kind == 0) { select(s.b); resetRotation(); }
    else toggleDelete();
}

std::string GameScreen::hotbarTooltip(Vector2 p) const {
    auto rs = hotbarRects();
    for (int i = 0; i < HOTBAR_N; ++i) {
        if (!inRect(rs[size_t(i)], p)) continue;
        const HotbarSlot& s = HOTBAR[i];
        std::string key = std::to_string(i + 1);
        if (s.kind == 1) return "Nothing (" + key + ")";
        if (s.kind == 2) return "Delete (" + key + " / X)";
        bool belt = s.b == Buildable::Belt;
        std::string name = belt ? "Belt" : machineSpec(toMachine(s.b)).name;
        const ItemAmounts& cost = belt ? beltBuildCost() : machineSpec(toMachine(s.b)).cost;
        std::string tip = name + " (" + key + ") - " + costText(cost);
        if (belt || s.b == Buildable::Splitter)   // one item per tile every BELT_TICKS_PER_TILE ticks
            tip += " - " + std::to_string(60 * TICKS_PER_SECOND / BELT_TICKS_PER_TILE) + " items/min - R / T to rotate";
        else if (machineTurnable(toMachine(s.b)))
            tip += " - R to turn it";
        return tip;
    }
    return "";
}

void GameScreen::drawHotbar() {
    auto rs = hotbarRects();
    for (int i = 0; i < HOTBAR_N; ++i) {
        Rectangle r = rs[size_t(i)];
        const HotbarSlot& s = HOTBAR[i];
        // (while copying / pasting / area deleting no slot is selected)
        bool single = bpMode_ == BpMode::None;
        bool active = (single && s.kind == 2 && mode_ == BuildMode::Deleting) || (s.kind == 1 && mode_ == BuildMode::None) ||
                      (single && s.kind == 0 && mode_ == BuildMode::Building && selected_ == s.b);
        drawButtonBase(r, active ? ui::ACTIVE_SLOT : rgba(55, 55, 55));
        lineRound(r, 8, 2, rgba(20, 20, 20));
        Rectangle inner{r.x + 8, r.y + 8, r.width - 16, r.height - 16};
        if (s.kind == 0) {
            const Texture2D* t = s.b == Buildable::Belt ? &assets::belt(0, Dir::Right) : assets::machine(toMachine(s.b));
            if (t) {
                float sc = std::min(inner.width / float(t->width), inner.height / float(t->height));
                float tw = t->width * sc, th = t->height * sc;
                drawTexture(*t, {r.x + (r.width - tw) / 2, r.y + (r.height - th) / 2, tw, th});
            } else {
                lineRound({r.x + 9, r.y + 9, r.width - 18, r.height - 18}, 4, 2, rgba(170, 170, 170));
            }
        } else if (s.kind == 1) {
            lineRound({r.x + 9, r.y + 9, r.width - 18, r.height - 18}, 4, 2, rgba(170, 170, 170));
        } else {
            // trash can
            Color red = rgba(220, 70, 70);
            Rectangle body = centeredRect(r.x + r.width / 2, r.y + r.height / 2 + 5, r.width - 26, r.height - 28);
            fillRound(body, 3, red);
            for (float lx = body.x + 6; lx < body.x + body.width - 5; lx += 6)
                DrawLineEx({lx, body.y + 4}, {lx, body.y + body.height - 4}, 2, rgba(150, 30, 30));
            fillRound(centeredRect(r.x + r.width / 2, body.y - 2, r.width - 16, 6), 2, red);
            fillRound(centeredRect(r.x + r.width / 2, body.y - 7, 14, 5), 2, red);
        }
        text(std::to_string(i + 1), r.x + 4, r.y + 2, 13, WHITE, true);
        if (mapMode() && s.kind != 1) fillRound(r, 8, Color{20, 20, 20, 160}); // unavailable in map view
    }
}

// ---------------------------------------------------------------------------
// Undo (Ctrl + Z)
// ---------------------------------------------------------------------------
void GameScreen::commitUndo() {
    UndoRecord r = world_.takeRecord();
    if (r.empty()) return;
    undoStack_.push_back(std::move(r));
    if (undoStack_.size() > UNDO_MAX) undoStack_.erase(undoStack_.begin());
    redoStack_.clear();   // something new happened: the old "future" is gone
}

void GameScreen::undoLast() {
    commitUndo();
    if (undoStack_.empty()) { notify("Nothing to undo"); return; }
    applyUndo(undoStack_, redoStack_, "undo");
}

void GameScreen::redoLast() {
    commitUndo();
    if (redoStack_.empty()) { notify("Nothing to redo"); return; }
    applyUndo(redoStack_, undoStack_, "redo");
}

void GameScreen::applyUndo(std::vector<UndoRecord>& from, std::vector<UndoRecord>& to, const char* verb) {
    const UndoRecord& rec = from.back();
    // where to show the message: the first thing it changes
    const ObjSnap& any = rec.removed.empty() ? rec.added.front() : rec.removed.front();
    float wx = float(any.pos.x * CELL_SIZE + CELL_SIZE / 2), wy = float(any.pos.y * CELL_SIZE);
    ItemAmounts delta;
    UndoRecord inverse;
    std::string v = verb;
    switch (builder_.undo(rec, &delta, &inverse)) {
        case Builder::UndoResult::Ok:
            from.pop_back();
            to.push_back(std::move(inverse));
            if (to.size() > UNDO_MAX) to.erase(to.begin());
            if (itemDeltaText(delta).empty()) floatText(wx, wy, v == "undo" ? "Undone" : "Redone", Color{120, 255, 140, 255});
            else showItemDelta(wx, wy, delta);
            break;
        case Builder::UndoResult::Changed:   // can never work any more
            from.pop_back();
            notify("Can't " + v + " that - it has been changed since");
            break;
        case Builder::UndoResult::Blocked: notify("Can't " + v + ": something else is in the way now"); break;
        case Builder::UndoResult::PlayerInTheWay: notify("Can't " + v + ": you are standing in the way"); break;
        case Builder::UndoResult::NoFunds: notify("Can't " + v + ": you don't have the items for it"); break;
        case Builder::UndoResult::NoSpace: notify("Can't " + v + ": not enough inventory space"); break;
        case Builder::UndoResult::Nothing: from.pop_back(); break;
    }
}

std::string GameScreen::autosaveName() const {
    if (save::isAutosave(saveName_)) return saveName_;   // an autosave whose save is gone: keep saving into it
    return save::autosaveName(saveName_);
}

SearchBox* GameScreen::focusedSearch() {
    for (SearchBox* sb : {&craftSearch_, &pickerSearch_, &bpSearch_, &machineSearch_})
        if (sb->focused()) return sb;
    return nullptr;
}
