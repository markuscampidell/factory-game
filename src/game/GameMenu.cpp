// game/GameMenu.cpp - the in-game "Menu" button, its panel, Save As, the
// exit confirmation and the Controls help (port of ui/game_menu_bar.py and
// ui/controls_panel.py).
#include "GameScreen.hpp"
#include "Assets.hpp"

namespace {
const char* INVALID_NAME_MESSAGE = "Save names can only contain letters, numbers, spaces, - and _.";

struct ControlEntry { const char* key; const char* desc; };
struct ControlSection { const char* title; std::vector<ControlEntry> entries; };

const std::vector<ControlSection>& controlSections() {
    static const std::vector<ControlSection> s = {
        {"Movement", {{"W / A / S / D, Arrow Keys", "Move around (in map view: pan the map)"}}},
        {"Zoom", {{"Scroll Wheel (over the world)", "Zoom in / out around the mouse"},
                  {"+ / -", "Zoom in / out"},
                  {"Minimap", "Bottom left: shows the area around you. Click it to open the map view"},
                  {"P", "Place a named map marker where the mouse is (shown on the map view and the minimap). P on a marker: rename or delete it"},
                  {"Drag (map view)", "Hold the left mouse button and move the mouse to move the map (middle button: also while copying / placing)"},
                  {"Click a marker", "Edit it: name, symbol (any sprite of the game, with search) or delete"},
                  {"Add map marker (button)", "Above Delete area: pick up a marker, move around (also in the map view) and left click to place it. Right click / ESC: cancel"},
                  {"Map view", "M, a click on the minimap, or keep zooming out: zooming stops at 40%; 10 more notches in a row switch to the map (a bar in the middle of the screen fills up; it empties after a 1 s pause). If you zoom out fast, the zoom first stays at 40% for a quarter of a second, then the notches start counting. Leaving works the same way, zooming in. Zoomed far out, everything built is drawn as plain boxes. You can't move, build single things or delete single things there, but Copy / Paste (Ctrl + C / V), Delete area (Ctrl + X) and blueprints work"}}},
        {"Menus (ESC)", {{"ESC", "Close the open panels (an open item picker first), then leave build mode, then open the Game Menu. In the Game Menu: one step back"},
                         {"TAB / Inventory", "Toggle your inventory (button on the left edge)"},
                         {"F / Handcrafting", "Toggle the hand-crafting panel (button on the right edge)"},
                         {"Controls", "This list (button next to Menu, top right)"},
                         {"Autosave", "Every 5 minutes of play the game is saved under its own name, just like pressing Save"}}},
        {"Building (bottom-of-screen hotbar)",
         {{"1 - 6", "Select a buildable: Smelter, Assembler, Belt, Splitter, Storage, Miner"},
          {"7", "Select Nothing - deselects, same as clicking that hotbar slot"},
          {"8 / X", "Toggle delete mode"},
          {"Click a hotbar slot", "Same as pressing its number - the row is always clickable"},
          {"Alt / Icons button (bottom right)", "Show / hide the output icons on machines and the item icons on chests"},
          {"M / minimap (bottom left)", "Map view on / off (back to the zoom you had)"},
          {"Blueprints button (bottom right)", "Open the blueprint list (same as B)"},
          {"C", "Point at a machine or belt and press this to pick its type into the hotbar"},
          {"Middle Click", "Pick up a machine/belt/splitter (with its direction) to build more of it. Recipe and filters stay default; not saved, Ctrl + V ignores it"},
          {"R", "Rotate the selected building clockwise (belts, splitters, smelters / assemblers - their output sides -, and a held copy / blueprint; a miner switches between standing and lying)"},
          {"R / Shift + R / T (pointing at something placed)", "Not building: turn the belt, splitter, smelter or assembler under the mouse - everything inside stays (items, filters, recipe). Ctrl + Z undoes it"},
          {"Output sides", "Smelters and assemblers put their products only onto belts at their two green arrows (right + top until you turn them) - pointing away or running past. Red arrows mark the input sides (left + bottom), but they take inputs from every side. Miners output on every side, chests only onto belts pointing straight away. The arrows show while building and on the machine under the mouse"},
          {"Shift + R", "Rotate the selected building counter-clockwise"},
          {"T", "Rotate the selected building 180 degrees"},
          {"G", "While holding a copy / blueprint: mirror it left <-> right"},
          {"Ctrl + Z", "Undo the last build, delete, paste or area delete (up to 50 steps). Everything comes back exactly - you pay for it again, or get it back. (On a German keyboard Ctrl + Y does the same - it's the same key)"},
          {"Ctrl + R", "Redo what you just undid (plain R still rotates). A new build / delete clears what could be redone"},
          {"Left Click", "Place the selected building - or delete under the cursor in delete mode"},
          {"Left Click (belts)", "First click starts a belt line, second click finishes it"},
          {"Shift (while building or pasting)", "Replace whatever is in the way. Belts and machines of the same kind are turned/updated in place and keep their items (a machine only hands out its items if its recipe changes); everything else is refunded"},
          {"Shift + Left Click", "In delete mode, delete an entire connected belt run at once (the delete-mode hint says so too)"},
          {"Holding a belt", "Arrows on the sides of nearby machines: a double arrow = a belt there can go in or out, a single one = only in (input side) or only out (miners); a placed belt shows green 'out' (the machine pushes onto it) or blue 'in' (it feeds the machine)"},
          {"Right Click", "Cancel the current placement, or exit build/delete mode"}}},
        {"Mining & Copy settings", {{"Hold Right Click (on ore)", "Mine raw resources by hand (half the speed of a Miner)"},
                              {"Shift + Right Click", "Copy settings: a machine's recipe, a belt's filter or a splitter's filters"},
                              {"Shift + Left Click (hold)", "Paste settings onto the machine/belt/splitter - keep holding and sweep the mouse to paste onto many"},
                              {"Ctrl + Left Click (hold)", "Take a machine's outputs or everything in a chest into your inventory (no panel opens) - keep holding and sweep the mouse to take from many"},
                              {"E (press or hold)", "Pick up the items on the belts and splitters you are standing on - hold it and walk along a belt to keep picking up"}}},
        {"Copy area, Paste & Blueprints",
         {{"Copy area / Paste / Delete area", "Buttons above the bottom right buttons - the same as Ctrl + C / Ctrl + V / Ctrl + X. Click the lit-up one again to stop"},
          {"Ctrl + C", "Copy area: drag a rectangle over machines/belts, then click to place the copy (R / T turn it, G mirrors it)"},
          {"Ctrl + X", "Delete many things: drag a rectangle - everything inside is removed and refunded (only if it all fits in your inventory). Right click: normal deleting"},
          {"Ctrl + V", "Place the last copy again (needs free space or identical things underneath, and the items in your inventory)"},
          {"Ctrl + B", "Make a blueprint: drag a rectangle, then name it and press Save blueprint (or Reselect). ESC / click outside cancels"},
          {"B", "Open the blueprint list: Add, Use or Delete blueprints"},
          {"Add blueprint book", "In the blueprint list: make a named book. Click its headline to open / close it; drag a blueprint onto a book to put it in, or onto empty space to take it out. Deleting a book deletes the blueprints in it. 'Add blueprint' on a book saves a new blueprint straight into it"},
          {"Configure", "On a blueprint in the list: rename it or Reselect its area"},
          {"Placing area", "Blueprints and copies can only be placed in your chunk and 4 chunks around it (9 x 9 chunks). While you hold one, that square is outlined"},
          {"Right Click", "While copying/placing: back to normal building"},
          {"ESC", "Cancel copying/placing completely"}}},
        {"Crafting", {{"Left Click", "Select a recipe, or press Produce/Cancel"},
                      {"Search boxes", "Handcrafting, the item picker and the blueprint list have one: click it and type to filter. While you type, keys don't do anything else. Enter / ESC: stop typing; the x clears it"},
                      {"SPACE", "Toggle auto-craft for the selected recipe"}}},
        {"Inventory & Storage",
         {{"Left Click", "Open a machine's, storage's, splitter's, or belt's panel by clicking its tile"},
          {"Shift + Left Click", "Move one stack between the open panel and your inventory"},
          {"Ctrl + Left Click", "Move every stack of that item type between the open panel and your inventory"},
          {"Left Click (outside a panel)", "Close that panel"},
          {"Scroll Wheel", "Scroll a panel's contents, like this one"}}},
        {"Belt & Splitter Filters", {{"Left Click (checkbox)", "Toggle whether that filter is enabled"},
                                     {"Left Click (a slot)", "Open the item picker to choose what that slot filters for"},
                                     {"Right Click (a filled slot)", "Clear that slot"}}},
    };
    return s;
}

std::vector<std::string> wrap(const std::string& s, int size, float maxW) {
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        std::string word = s.substr(i, j - i);
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (measure(cand, size).x <= maxW || cur.empty()) cur = cand;
        else { lines.push_back(cur); cur = word; }
        i = j + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

Rectangle menuButtonRect(float w) { return {w - 110, 10, 100, 32}; }
Rectangle menuPanelRect(float w, float h) { return centeredRect(std::floor(w / 2), std::floor(h / 2), 320, 340); }
Rectangle saveAsPanelRect(float w, float h) { return centeredRect(std::floor(w / 2), std::floor(h / 2), 360, 160); }
} // namespace

// ---------------------------------------------------------------------------
// HUD buttons: Menu (top right), Blueprints and Alt (bottom right, level
// with the hotbar)
// ---------------------------------------------------------------------------
Rectangle GameScreen::blueprintButtonRect() const { return {ui::screenW() - 10 - 120, ui::screenH() - 62, 120, 32}; }
Rectangle GameScreen::altButtonRect() const { return {ui::screenW() - 10 - 120 - 10 - 60, ui::screenH() - 62, 60, 32}; }
// Controls: left of Menu (top right)
Rectangle GameScreen::controlsButtonRect() const { return {ui::screenW() - 110 - 10 - 110, 10, 110, 32}; }
// Copy / Paste / Delete area: one row above Map / Alt / Blueprints (high
// enough to stay clear of the hotbar)
namespace { constexpr float EDIT_ROW_UP = 108, CUT_W = 130, COPY_W = 80, COPY_AREA_W = 110; }
Rectangle GameScreen::cutButtonRect() const { return {ui::screenW() - 10 - CUT_W, ui::screenH() - EDIT_ROW_UP, CUT_W, 32}; }
Rectangle GameScreen::pasteButtonRect() const {
    return {ui::screenW() - 10 - CUT_W - 10 - COPY_W, ui::screenH() - EDIT_ROW_UP, COPY_W, 32};
}
// "Add map marker": above Delete area
Rectangle GameScreen::markerButtonRect() const { return {ui::screenW() - 10 - CUT_W, ui::screenH() - EDIT_ROW_UP - 42, CUT_W, 32}; }
Rectangle GameScreen::copyButtonRect() const {
    return {ui::screenW() - 10 - CUT_W - 10 - COPY_W - 10 - COPY_AREA_W, ui::screenH() - EDIT_ROW_UP, COPY_AREA_W, 32};
}
// Inventory (left edge) and Handcrafting (right edge), level with the
// middle of the panels. Narrow enough that even the widest panel (splitter
// filter) never covers them.
namespace { constexpr float EDGE_W = 116, EDGE_H = 40; }
Rectangle GameScreen::inventoryButtonRect() const { return {10, std::floor(ui::panelCenterY() - EDGE_H / 2), EDGE_W, EDGE_H}; }
Rectangle GameScreen::craftButtonRect() const {
    return {ui::screenW() - 10 - EDGE_W, std::floor(ui::panelCenterY() - EDGE_H / 2), EDGE_W, EDGE_H};
}

bool GameScreen::overHudButtons(Vector2 p) const {
    if (inRect(menuButtonRect(ui::screenW()), p) || inRect(controlsButtonRect(), p) || inRect(blueprintButtonRect(), p) ||
        inRect(altButtonRect(), p) || inRect(copyButtonRect(), p) || inRect(markerButtonRect(), p) ||
        inRect(pasteButtonRect(), p) || inRect(cutButtonRect(), p))
        return true;
    // these sit under the panels: an open panel on top of them wins
    if (mouseOverOpenPanel(p)) return false;
    return inRect(inventoryButtonRect(), p) || inRect(craftButtonRect(), p) || (minimapVisible() && inRect(minimapRect(), p));
}

std::string GameScreen::hudButtonTooltip(Vector2 p) const {
    if (inRect(controlsButtonRect(), p)) return "Show all keyboard and mouse controls";
    if (inRect(markerButtonRect(), p))
        return markerPlacing_ ? "Click a spot in the world to place the marker - right click / ESC cancels"
                              : "Add map marker: pick it up, move around and left click to place it (or press P at the mouse)";
    if (inRect(copyButtonRect(), p))
        return "Copy area (Ctrl + C): drag a rectangle over machines and belts, then click to place the copy";
    if (inRect(pasteButtonRect(), p))
        return clipboard_.empty() ? "Paste (Ctrl + V): nothing copied yet - use Copy area first"
                                  : "Paste (Ctrl + V): place your last copied area again";
    if (inRect(cutButtonRect(), p))
        return "Delete area (Ctrl + X): drag a rectangle - everything inside is removed and refunded";
    if (!mouseOverOpenPanel(p)) {
        if (inRect(inventoryButtonRect(), p))
            return invOpen_ ? "Close your inventory (TAB)" : "Open your inventory (TAB)";
        if (inRect(craftButtonRect(), p))
            return craftOpen_ ? "Close handcrafting (F)"
                              : "Handcrafting (F): pick a recipe and press Produce - SPACE toggles auto-craft";
        if (minimapVisible() && inRect(minimapRect(), p))
            return mapMode() ? "Minimap - click to go back to the normal view (M)" : "Minimap - click to open the map view (M)";
    }
    if (inRect(blueprintButtonRect(), p)) return "Open your blueprints (B)";
    if (inRect(altButtonRect(), p))
        return showOutputs_ ? "Icons (Alt): machine outputs and chest contents are shown - click or press Alt to hide them"
                            : "Icons (Alt): machine outputs and chest contents are hidden - click or press Alt to show them";
    if (inRect(menuButtonRect(ui::screenW()), p)) return "Game menu (ESC)";
    return "";
}

// Names of hotbar slots and top right buttons under the mouse. Drawn last so
// they are on top of everything; hidden while a dialog is open.
void GameScreen::drawHudTooltips() {
    if (menuOpen_ || saveAsOpen_ || controlsOpen_ || exitConfirm_ || bpDeleteConfirm_ || bpSaveOpen_ || bpBookDialogOpen_ ||
        markerDialogOpen_)
        return;
    Vector2 m = ui::mouse();
    std::string tip = hotbarTooltip(m);
    if (tip.empty()) tip = hudButtonTooltip(m);
    // an ore tile you could mine (hoveredOre_ is only set when that's possible)
    if (tip.empty() && hoveredOre_ && !mining_) tip = "Hold right click to mine " + itemDef(world_.oreAt(*hoveredOre_)).name;
    // a map marker: its name (in the map view the name is drawn anyway)
    if (tip.empty() && !mapMode()) {
        int mk = markerAt(m);
        if (mk >= 0) tip = markers_[size_t(mk)].name + "  (click to edit)";
    }
    if (!tip.empty()) drawTooltip(tip, m);
}

void GameScreen::openSaveAs(const std::string& initial) {
    saveAsInput_ = TextInput(centeredRect(std::floor(ui::screenW() / 2), std::floor(ui::screenH() / 2) - 10, 320, 44), initial);
    saveAsOpen_ = true;
}

void GameScreen::submitSaveAs(const std::string& raw) {
    if (!save::isValidName(raw)) {
        saveAsMessage_ = std::make_unique<MessageDialog>(INVALID_NAME_MESSAGE);
        return;
    }
    std::string name = save::sanitizeName(raw);
    if (save::exists(name) && name != saveName_) {
        pendingSaveAs_ = name;
        saveAsConfirm_ = std::make_unique<ConfirmDialog>("Overwrite save '" + name + "'?");
        saveAsInput_.reset();
        return;
    }
    saveNow(name);
    saveName_ = name;
    save::setLastOpened(name);
    saveAsOpen_ = false;          // back to the Game Menu
    saveAsInput_.reset();
    notify("Saved as '" + name + "'");
}

bool GameScreen::menuBarHandle(const Event& e) {
    float w = ui::screenW(), h = ui::screenH();
    if (exitConfirm_) {
        DialogResult r = exitConfirm_->handle(e);
        if (r == DialogResult::Yes) { saveNow(saveName_); returnToMenu_ = true; exitConfirm_.reset(); menuOpen_ = false; }
        else if (r == DialogResult::No) { returnToMenu_ = true; exitConfirm_.reset(); menuOpen_ = false; }
        else if (r == DialogResult::Cancel) exitConfirm_.reset();
        return true;
    }
    if (controlsOpen_) { controlsHandle(e); return true; }
    if (saveAsMessage_) {
        if (saveAsMessage_->handle(e)) saveAsMessage_.reset();
        return true;
    }
    if (saveAsConfirm_) {
        DialogResult r = saveAsConfirm_->handle(e);
        if (r == DialogResult::Yes) {
            saveNow(pendingSaveAs_);
            saveName_ = pendingSaveAs_;
            save::setLastOpened(saveName_);
            saveAsConfirm_.reset();
            notify("Saved as '" + pendingSaveAs_ + "'");
            pendingSaveAs_.clear();
            saveAsOpen_ = false;      // back to the Game Menu
            saveAsInput_.reset();
        } else if (r == DialogResult::No || r == DialogResult::Cancel) {
            std::string name = pendingSaveAs_;
            saveAsConfirm_.reset();
            pendingSaveAs_.clear();
            openSaveAs(name);
        }
        return true;
    }
    if (saveAsOpen_) {
        Rectangle panel = saveAsPanelRect(w, h);
        if (e.type == EvType::MouseDown && e.button == MB_LEFT && (!inRect(panel, e.pos) || inRect(panelCloseRect(panel), e.pos))) {
            saveAsOpen_ = false;
            saveAsInput_.reset();
            return true;
        }
        saveAsInput_->handle(e);
        Rectangle confirm = centeredRect(panel.x + panel.width / 2 - 70, panel.y + panel.height - 34, 120, 40);
        Rectangle cancel = centeredRect(panel.x + panel.width / 2 + 70, panel.y + panel.height - 34, 120, 40);
        if (saveAsInput_->submitted) {
            saveAsInput_->submitted = false;
            std::string name = saveAsInput_->text;
            if (name.find_first_not_of(' ') != std::string::npos) submitSaveAs(name);
        } else if (saveAsInput_->cancelled) {
            saveAsOpen_ = false;
            saveAsInput_.reset();
        } else if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
            if (inRect(confirm, e.pos)) {
                std::string name = saveAsInput_->text;
                if (name.find_first_not_of(' ') != std::string::npos) submitSaveAs(name);
            } else if (inRect(cancel, e.pos)) {
                saveAsOpen_ = false;
                saveAsInput_.reset();
            }
        }
        return true;
    }
    if (menuOpen_) {
        if (e.type == EvType::KeyDown && e.key == KEY_ESCAPE) { menuOpen_ = false; return true; }
        if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
            Rectangle panel = menuPanelRect(w, h);
            float cx = panel.x + panel.width / 2;
            Rectangle save = centeredRect(cx, panel.y + 90, 240, 44);
            Rectangle saveAs = centeredRect(cx, panel.y + 150, 240, 44);
            Rectangle saveExit = centeredRect(cx, panel.y + 210, 240, 44);
            Rectangle exitR = centeredRect(cx, panel.y + 270, 240, 44);
            Rectangle closeX = panelCloseRect(panel);
            if (!inRect(panel, e.pos)) menuOpen_ = false;
            else if (inRect(closeX, e.pos)) menuOpen_ = false;
            else if (inRect(save, e.pos)) { saveNow(saveName_); notify("Saved '" + saveName_ + "'"); }   // menu stays open
            else if (inRect(saveExit, e.pos)) { saveNow(saveName_); returnToMenu_ = true; menuOpen_ = false; }
            else if (inRect(saveAs, e.pos)) openSaveAs(saveName_);
            else if (inRect(exitR, e.pos)) exitConfirm_ = std::make_unique<ConfirmDialog>("Save before exiting?", true);   // X = don't exit
        }
        return true; // modal
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(menuButtonRect(w), e.pos)) {
        closeAllPanels();
        menuOpen_ = true;
        return true;
    }
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return false;
    if (bpSaveOpen_ || bpBookDialogOpen_ || bpDeleteConfirm_ || markerDialogOpen_) return false;   // blueprintHandle has them
    if (inRect(markerButtonRect(), e.pos)) {
        bool wasPlacing = markerPlacing_;
        closeAllPanels();
        resetBuildState();
        markerPlacing_ = !wasPlacing;   // click again: put it back
        return true;
    }
    if (inRect(controlsButtonRect(), e.pos)) {
        closeAllPanels();
        controlsOpen_ = true;
        controlsScroll_ = 0;
        controlsFold_.closeAll();   // every headline starts closed
        return true;
    }
    // Copy / Paste / Delete area: same as Ctrl + C / V / X. Clicking the
    // one that is active cancels it.
    if (inRect(copyButtonRect(), e.pos)) {
        if (bpMode_ == BpMode::Selecting && !bpForList_) resetBuildState();
        else startSelecting(false);
        return true;
    }
    if (inRect(pasteButtonRect(), e.pos)) {
        if (bpMode_ == BpMode::Placing) resetBuildState();
        else if (clipboard_.empty()) notify("Nothing copied yet - use Copy (Ctrl + C) first");
        else startPlacing(clipboard_);
        return true;
    }
    if (inRect(cutButtonRect(), e.pos)) {
        if (bpMode_ == BpMode::Cutting) resetBuildState();
        else startCutting();
        return true;
    }
    // the edge buttons and the minimap are under the panels
    if (!mouseOverOpenPanel(e.pos)) {
        if (inRect(inventoryButtonRect(), e.pos)) { toggleInventory(); return true; }
        if (inRect(craftButtonRect(), e.pos)) { toggleCrafting(); return true; }
        if (minimapVisible() && inRect(minimapRect(), e.pos)) { toggleMap(); return true; }
    }
    if (inRect(blueprintButtonRect(), e.pos)) {
        if (bpListOpen_) closePanel(Panel::Blueprints);   // same as pressing B
        else openBlueprintList();
        return true;
    }
    if (inRect(altButtonRect(), e.pos)) {
        showOutputs_ = !showOutputs_;
        return true;
    }
    return false;
}

void GameScreen::drawMenuBar() {
    float w = ui::screenW();
    // Menu / Blueprints light up while what they open is open
    auto hudButton = [](Rectangle r, const char* label, bool active) { drawButton(r, label, ui::BTN, 18, active); };
    hudButton(menuButtonRect(w), "Menu", menuOpen_ || saveAsOpen_ || exitConfirm_ != nullptr);
    hudButton(controlsButtonRect(), "Controls", controlsOpen_);
    // Copy / Paste / Delete area light up while you are doing that
    hudButton(copyButtonRect(), "Copy area", bpMode_ == BpMode::Selecting && !bpForList_);
    hudButton(pasteButtonRect(), "Paste", bpMode_ == BpMode::Placing);
    drawButton(cutButtonRect(), "Delete area", ui::BTN_DANGER, 18, bpMode_ == BpMode::Cutting);
    drawButton(markerButtonRect(), "Add map marker", ui::BTN, 16, markerPlacing_);
    if (clipboard_.empty() && bpMode_ != BpMode::Placing) fillRound(pasteButtonRect(), 8, Color{20, 20, 20, 110});
    hudButton(blueprintButtonRect(), "Blueprints", bpListOpen_);
    // Alt: lit up (like an active hotbar slot) while outputs are shown
    Rectangle alt = altButtonRect();
    drawButtonBase(alt, showOutputs_ ? ui::ACTIVE_SLOT : rgba(55, 55, 55));
    lineRound(alt, 8, 2, rgba(20, 20, 20));
    textCentered("Icons", alt.x + alt.width / 2, alt.y + alt.height / 2, 18, showOutputs_ ? BLACK : rgba(200, 200, 200));
}

// Inventory (left edge) and Handcrafting (right edge). Drawn before the
// panels, so an open panel always covers them.
void GameScreen::drawEdgeButtons() {
    drawButton(inventoryButtonRect(), "Inventory", ui::BTN, 16, invOpen_);
    drawButton(craftButtonRect(), "Handcrafting", ui::BTN, 16, craftOpen_);
}

bool GameScreen::anyModalOpen() const {
    return menuOpen_ || saveAsOpen_ || controlsOpen_ || exitConfirm_ || saveAsConfirm_ || saveAsMessage_ ||
           bpSaveOpen_ || bpDeleteConfirm_ || bpBookDialogOpen_ || markerDialogOpen_;
}

void GameScreen::drawModals(float dt) {
    float w = ui::screenW(), h = ui::screenH();
    // The Game Menu stays visible under Save As / Controls / the exit
    // question; only the topmost one reacts to the mouse.
    if (menuOpen_) {
        ui::setHoverBlocked(saveAsOpen_ || controlsOpen_ || exitConfirm_ || saveAsConfirm_ || saveAsMessage_);
        Rectangle panel = menuPanelRect(w, h);
        drawModalShade();
        drawPanel(panel, "Game Menu");
        float cx = panel.x + panel.width / 2;
        drawButton(centeredRect(cx, panel.y + 90, 240, 44), "Save", ui::BTN_OK);
        drawButton(centeredRect(cx, panel.y + 150, 240, 44), "Save As", ui::BTN);
        drawButton(centeredRect(cx, panel.y + 210, 240, 44), "Save and exit", ui::BTN_OK);
        drawButton(centeredRect(cx, panel.y + 270, 240, 44), "Exit", ui::BTN_DANGER);
    }
    if (saveAsOpen_) {
        ui::setHoverBlocked(saveAsConfirm_ || saveAsMessage_);
        Rectangle panel = saveAsPanelRect(w, h);
        drawModalShade();
        drawPanel(panel, "Save As");
        if (saveAsInput_) { saveAsInput_->update(dt); saveAsInput_->draw(); }
        Rectangle confirm = centeredRect(panel.x + panel.width / 2 - 70, panel.y + panel.height - 34, 120, 40);
        Rectangle cancel = centeredRect(panel.x + panel.width / 2 + 70, panel.y + panel.height - 34, 120, 40);
        drawButton(confirm, "Save", ui::BTN_OK);
        drawButton(cancel, "Cancel", ui::BTN_DANGER);
    }
    ui::setHoverBlocked(false);
    if (exitConfirm_) exitConfirm_->draw();
    if (saveAsConfirm_) saveAsConfirm_->draw();
    if (saveAsMessage_) saveAsMessage_->draw();
    if (controlsOpen_) drawControls();
    drawBpSaveDialog();
    drawBpBookDialog();
    drawMarkerDialog();
    if (bpDeleteConfirm_) bpDeleteConfirm_->draw();
}

// ---------------------------------------------------------------------------
// Controls panel
// ---------------------------------------------------------------------------
namespace {
// Every section is an expandable headline (all closed when Controls opens).
constexpr float CTRL_HEAD_H = 34, CTRL_HEAD_GAP = 6, CTRL_INDENT = 14;
struct ControlsLayout {
    Rectangle box, close, viewport;
    std::vector<float> headY;   // headline tops, relative to the content top
    float content = 0;
};
float entryHeight(const ControlEntry& en, float width) {
    float indent = measure(std::string(en.key) + ":", 18, true).x + 10;
    return float(wrap(en.desc, 18, std::max(40.0f, width - indent)).size()) * 28 + 10;
}
ControlsLayout controlsLayout(float w, float h, const Foldout& fold) {
    ControlsLayout L;
    L.box = centeredRect(std::floor(w / 2), std::floor(h / 2), 560, std::min(560.0f, h - 80));
    L.close = panelCloseRect(L.box);
    float top = L.box.y + ui::TITLE_H + 8;
    L.viewport = {L.box.x + 24, top, L.box.width - 48, L.box.y + L.box.height - 24 - top};
    float y = 0;
    for (auto& sec : controlSections()) {
        L.headY.push_back(y);
        y += CTRL_HEAD_H + CTRL_HEAD_GAP;
        if (!fold.isOpen(sec.title)) continue;
        y += 4;
        for (auto& en : sec.entries) y += entryHeight(en, L.viewport.width - CTRL_INDENT);
        y += 6;
    }
    L.content = y;
    return L;
}
} // namespace

bool GameScreen::controlsHandle(const Event& e) {
    ControlsLayout L = controlsLayout(ui::screenW(), ui::screenH(), controlsFold_);
    if (e.type == EvType::KeyDown && e.key == KEY_ESCAPE) { controlsOpen_ = false; return true; }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(L.close, e.pos) || !inRect(L.box, e.pos)) { controlsOpen_ = false; return true; }
        if (inRect(L.viewport, e.pos)) {
            const auto& secs = controlSections();
            for (size_t i = 0; i < secs.size(); ++i) {
                Rectangle head{L.viewport.x, L.viewport.y + L.headY[i] - controlsScroll_, L.viewport.width - 10, CTRL_HEAD_H};
                if (controlsFold_.handleClick(e, head, secs[i].title)) {
                    ControlsLayout L2 = controlsLayout(ui::screenW(), ui::screenH(), controlsFold_);
                    controlsScroll_ = clampScroll(controlsScroll_, L2.content, L2.viewport.height);
                    return true;
                }
            }
        }
    }
    if (e.type == EvType::Wheel && inRect(L.box, e.pos)) {
        controlsScroll_ = clampScroll(controlsScroll_ - e.wheel * 40, L.content, L.viewport.height);
        return true;
    }
    return false;
}

void GameScreen::drawControls() {
    ControlsLayout L = controlsLayout(ui::screenW(), ui::screenH(), controlsFold_);
    drawModalShade();
    drawPanel(L.box, "Controls");
    controlsScroll_ = clampScroll(controlsScroll_, L.content, L.viewport.height);

    ui::beginScissor({L.viewport.x, L.viewport.y, L.viewport.width, L.viewport.height});
    const auto& secs = controlSections();
    FoldHeaderStyle style;
    style.text = rgba(40, 40, 90);
    for (size_t i = 0; i < secs.size(); ++i) {
        const ControlSection& sec = secs[i];
        bool open = controlsFold_.isOpen(sec.title);
        float y = L.viewport.y + L.headY[i] - controlsScroll_;
        drawFoldHeader({L.viewport.x, y, L.viewport.width - 10, CTRL_HEAD_H}, sec.title, open, style);
        if (!open) continue;
        y += CTRL_HEAD_H + CTRL_HEAD_GAP + 4;
        float x = L.viewport.x + CTRL_INDENT, width = L.viewport.width - CTRL_INDENT;
        for (auto& en : sec.entries) {
            std::string key = std::string(en.key) + ":";
            float indent = measure(key, 18, true).x + 10;
            auto lines = wrap(en.desc, 18, std::max(40.0f, width - indent));
            text(key, x, y, 18, rgba(15, 15, 60), true);
            for (size_t k = 0; k < lines.size(); ++k) text(lines[k], x + indent, y + float(k) * 28, 18, BLACK);
            y += entryHeight(en, width);
        }
    }
    EndScissorMode();
    if (L.content > L.viewport.height)
        drawScrollbar({L.viewport.x + L.viewport.width + 6, L.viewport.y, 6, L.viewport.height}, controlsScroll_, L.content, L.viewport.height);
}
