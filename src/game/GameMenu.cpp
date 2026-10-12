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
                  {"Map view", "Zoomed far out, everything built is drawn as plain boxes. You can't move, build or delete there - zoom back in to continue"}}},
        {"Menus (ESC)", {{"ESC", "Close the open panels (an open item picker first), then leave build mode, then open the Game Menu. In the Game Menu: one step back"},
                         {"TAB", "Toggle your inventory"},
                         {"F", "Toggle the hand-crafting panel"}}},
        {"Building (bottom-of-screen hotbar)",
         {{"1 - 6", "Select a buildable: Smelter, Assembler, Belt, Splitter, Storage, Miner"},
          {"7", "Select Nothing - deselects, same as clicking that hotbar slot"},
          {"8 / X", "Toggle delete mode"},
          {"Click a hotbar slot", "Same as pressing its number - the row is always clickable"},
          {"Alt / Alt button (bottom right)", "Show / hide the output icons on machines"},
          {"M / Map button (bottom right)", "Map view on / off (back to the zoom you had)"},
          {"Blueprints button (bottom right)", "Open the blueprint list (same as B)"},
          {"C", "Point at a machine or belt and press this to pick its type into the hotbar"},
          {"Middle Click", "Pick up a machine/belt/splitter (with its direction) to build more of it. Recipe and filters stay default; not saved, Ctrl + V ignores it"},
          {"R", "Rotate the selected building clockwise"},
          {"Shift + R", "Rotate the selected building counter-clockwise"},
          {"T", "Rotate the selected building 180 degrees"},
          {"Left Click", "Place the selected building - or delete under the cursor in delete mode"},
          {"Left Click (belts)", "First click starts a belt line, second click finishes it"},
          {"Shift (while building or pasting)", "Replace whatever is in the way. Belts and machines of the same kind are turned/updated in place and keep their items (a machine only hands out its items if its recipe changes); everything else is refunded"},
          {"Shift + Left Click", "In delete mode, delete an entire connected belt run at once"},
          {"Right Click", "Cancel the current placement, or exit build/delete mode"}}},
        {"Mining & Recipes", {{"Hold Right Click (on ore)", "Mine raw resources by hand (half the speed of a Miner)"},
                              {"Shift + Right Click", "Copy a machine's recipe, a belt's filter or a splitter's filters"},
                              {"Shift + Left Click (hold)", "Paste onto the machine/belt/splitter - keep holding and sweep the mouse to paste onto many"}}},
        {"Copy, Paste & Blueprints",
         {{"Ctrl + C", "Copy many things: drag a rectangle over machines/belts, then click to place the copy"},
          {"Ctrl + X", "Delete many things: drag a rectangle - everything inside is removed and refunded (only if it all fits in your inventory). Right click: normal deleting"},
          {"Ctrl + V", "Place the last copy again (needs free space or identical things underneath, and the items in your inventory)"},
          {"Ctrl + B", "Make a blueprint: drag a rectangle, then name it and press Save blueprint (or Reselect). ESC / click outside cancels"},
          {"B", "Open the blueprint list: Add, Use or Delete blueprints"},
          {"Right Click", "While copying/placing: back to normal building"},
          {"ESC", "Cancel copying/placing completely"}}},
        {"Crafting", {{"Left Click", "Select a recipe, or press Produce/Cancel"},
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
Rectangle GameScreen::mapButtonRect() const { return {ui::screenW() - 10 - 120 - 10 - 60 - 10 - 70, ui::screenH() - 62, 70, 32}; }

bool GameScreen::overHudButtons(Vector2 p) const {
    return inRect(menuButtonRect(ui::screenW()), p) || inRect(blueprintButtonRect(), p) || inRect(altButtonRect(), p) ||
           inRect(mapButtonRect(), p);
}

std::string GameScreen::hudButtonTooltip(Vector2 p) const {
    if (inRect(blueprintButtonRect(), p)) return "Open your blueprints (B)";
    if (inRect(altButtonRect(), p))
        return showOutputs_ ? "Machine outputs are shown - click or press Alt to hide them"
                            : "Machine outputs are hidden - click or press Alt to show them";
    if (inRect(menuButtonRect(ui::screenW()), p)) return "Game menu (ESC)";
    if (inRect(mapButtonRect(), p)) return mapMode() ? "Back to the normal view (M)" : "Map view (M)";
    return "";
}

// Names of hotbar slots and top right buttons under the mouse. Drawn last so
// they are on top of everything; hidden while a dialog is open.
void GameScreen::drawHudTooltips() {
    if (menuOpen_ || saveAsOpen_ || controlsOpen_ || exitConfirm_ || bpDeleteConfirm_ || bpSaveOpen_) return;
    Vector2 m = ui::mouse();
    std::string tip = hotbarTooltip(m);
    if (tip.empty()) tip = hudButtonTooltip(m);
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
            Rectangle saveExit = centeredRect(cx, panel.y + 90, 240, 44);
            Rectangle saveAs = centeredRect(cx, panel.y + 150, 240, 44);
            Rectangle controls = centeredRect(cx, panel.y + 210, 240, 44);
            Rectangle exitR = centeredRect(cx, panel.y + 270, 240, 44);
            Rectangle closeX = panelCloseRect(panel);
            if (!inRect(panel, e.pos)) menuOpen_ = false;
            else if (inRect(closeX, e.pos)) menuOpen_ = false;
            else if (inRect(saveExit, e.pos)) { saveNow(saveName_); returnToMenu_ = true; menuOpen_ = false; }
            else if (inRect(saveAs, e.pos)) openSaveAs(saveName_);
            else if (inRect(controls, e.pos)) { controlsOpen_ = true; controlsScroll_ = 0; }
            else if (inRect(exitR, e.pos)) exitConfirm_ = std::make_unique<ConfirmDialog>("Save before exiting?", true);   // X = don't exit
        }
        return true; // modal
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(menuButtonRect(w), e.pos)) {
        closeAllPanels();
        menuOpen_ = true;
        return true;
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(blueprintButtonRect(), e.pos)) {
        if (bpListOpen_) closePanel(Panel::Blueprints);   // same as pressing B
        else openBlueprintList();
        return true;
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(mapButtonRect(), e.pos)) {
        toggleMap();
        return true;
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(altButtonRect(), e.pos)) {
        showOutputs_ = !showOutputs_;
        return true;
    }
    return false;
}

void GameScreen::drawMenuBar() {
    float w = ui::screenW();
    // Menu / Blueprints light up while what they open is open
    auto hudButton = [](Rectangle r, const char* label, bool active) { drawButton(r, label, ui::BTN, 18, active); };
    hudButton(menuButtonRect(w), "Menu", menuOpen_ || saveAsOpen_ || controlsOpen_ || exitConfirm_ != nullptr);
    hudButton(blueprintButtonRect(), "Blueprints", bpListOpen_);
    hudButton(mapButtonRect(), "Map", mapMode());
    // Alt: lit up (like an active hotbar slot) while outputs are shown
    Rectangle alt = altButtonRect();
    drawButtonBase(alt, showOutputs_ ? ui::ACTIVE_SLOT : rgba(55, 55, 55));
    lineRound(alt, 8, 2, rgba(20, 20, 20));
    textCentered("Alt", alt.x + alt.width / 2, alt.y + alt.height / 2, 18, showOutputs_ ? BLACK : rgba(200, 200, 200));

}

bool GameScreen::anyModalOpen() const {
    return menuOpen_ || saveAsOpen_ || controlsOpen_ || exitConfirm_ || saveAsConfirm_ || saveAsMessage_ ||
           bpSaveOpen_ || bpDeleteConfirm_;
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
        drawButton(centeredRect(cx, panel.y + 90, 240, 44), "Save and exit", ui::BTN_OK);
        drawButton(centeredRect(cx, panel.y + 150, 240, 44), "Save As", ui::BTN);
        drawButton(centeredRect(cx, panel.y + 210, 240, 44), "Controls", ui::BTN);
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
    if (bpDeleteConfirm_) bpDeleteConfirm_->draw();
}

// ---------------------------------------------------------------------------
// Controls panel
// ---------------------------------------------------------------------------
namespace {
struct ControlsLayout {
    Rectangle box, close, viewport;
    float content = 0;
};
ControlsLayout controlsLayout(float w, float h) {
    ControlsLayout L;
    L.box = centeredRect(std::floor(w / 2), std::floor(h / 2), 560, std::min(560.0f, h - 80));
    L.close = panelCloseRect(L.box);
    float top = L.box.y + ui::TITLE_H + 8;
    L.viewport = {L.box.x + 24, top, L.box.width - 48, L.box.y + L.box.height - 24 - top};
    float y = 0;
    bool first = true;
    for (auto& sec : controlSections()) {
        if (!first) y += 20;
        first = false;
        y += 20 + 4 + 10;
        for (auto& en : sec.entries) {
            float indent = measure(std::string(en.key) + ":", 18, true).x + 10;
            auto lines = wrap(en.desc, 18, std::max(40.0f, L.viewport.width - indent));
            y += float(lines.size()) * (22 + 6) + 10;
        }
    }
    L.content = y;
    return L;
}
} // namespace

bool GameScreen::controlsHandle(const Event& e) {
    ControlsLayout L = controlsLayout(ui::screenW(), ui::screenH());
    if (e.type == EvType::KeyDown && e.key == KEY_ESCAPE) { controlsOpen_ = false; return true; }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(L.close, e.pos) || !inRect(L.box, e.pos)) { controlsOpen_ = false; return true; }
    }
    if (e.type == EvType::Wheel && inRect(L.box, e.pos)) {
        controlsScroll_ = clampScroll(controlsScroll_ - e.wheel * 40, L.content, L.viewport.height);
        return true;
    }
    return false;
}

void GameScreen::drawControls() {
    ControlsLayout L = controlsLayout(ui::screenW(), ui::screenH());
    drawModalShade();
    drawPanel(L.box, "Controls");
    controlsScroll_ = clampScroll(controlsScroll_, L.content, L.viewport.height);

    ui::beginScissor({L.viewport.x, L.viewport.y, L.viewport.width, L.viewport.height});
    float y = L.viewport.y - controlsScroll_;
    bool first = true;
    for (auto& sec : controlSections()) {
        if (!first) y += 20;
        first = false;
        text(sec.title, L.viewport.x, y, 20, rgba(40, 40, 90), true);
        y += 20 + 4 + 10;
        for (auto& en : sec.entries) {
            std::string key = std::string(en.key) + ":";
            float indent = measure(key, 18, true).x + 10;
            auto lines = wrap(en.desc, 18, std::max(40.0f, L.viewport.width - indent));
            text(key, L.viewport.x, y, 18, rgba(15, 15, 60), true);
            for (size_t i = 0; i < lines.size(); ++i) text(lines[i], L.viewport.x + indent, y + float(i) * 28, 18, BLACK);
            y += float(lines.size()) * 28 + 10;
        }
    }
    EndScissorMode();
    if (L.content > L.viewport.height)
        drawScrollbar({L.viewport.x + L.viewport.width + 6, L.viewport.y, 6, L.viewport.height}, controlsScroll_, L.content, L.viewport.height);
}
