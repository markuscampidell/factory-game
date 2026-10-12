// game/MainMenu.cpp - port of game/main_menu.py
#include "MainMenu.hpp"
#include "../sim/Save.hpp"

static const char* INVALID_NAME_MESSAGE = "Save names can only contain letters, numbers, spaces, - and _.";

MainMenu::MainMenu() { refresh(); }

void MainMenu::refresh() {
    saves_ = save::listSaves();
    lastOpened_ = save::lastOpened();
}

void MainMenu::clearModal() {
    confirm_.reset();
    confirmAction_ = Confirm::None;
    deleteTarget_.clear();
    renameTarget_.clear();
    renameNewName_.clear();
    pendingNewGame_.clear();
}

MenuAction MainMenu::handle(const Event& e) {
    if (message_) {
        if (message_->handle(e)) message_.reset();
        return {};
    }
    if (confirm_) {
        DialogResult r = confirm_->handle(e);
        if (r == DialogResult::Yes) {
            if (confirmAction_ == Confirm::Delete) {
                save::remove(deleteTarget_);
                refresh();
                clearModal();
            } else if (confirmAction_ == Confirm::Rename) {
                save::remove(renameNewName_);
                save::rename(renameTarget_, renameNewName_);
                refresh();
                clearModal();
            } else if (confirmAction_ == Confirm::NewGame) {
                std::string name = pendingNewGame_;
                clearModal();
                screen_ = Screen::Root;
                return {MenuAction::NewGame, name};
            }
        } else if (r == DialogResult::No || r == DialogResult::Cancel) {
            if (confirmAction_ == Confirm::NewGame) {
                std::string name = pendingNewGame_;
                clearModal();
                openNewGame(name);
            } else {
                clearModal();
            }
        }
        return {};
    }
    if (renameInput_) return handleRename(e);
    switch (screen_) {
        case Screen::Root: return handleRoot(e);
        case Screen::NewGame: return handleNewGame(e);
        case Screen::LoadGame: return handleLoadGame(e);
    }
    return {};
}

MenuAction MainMenu::handleRoot(const Event& e) {
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return {};
    for (auto& b : rootButtons_) {
        if (!inRect(b.second, e.pos)) continue;
        if (b.first == "continue") return {MenuAction::LoadGame, lastOpened_};
        if (b.first == "new_game") openNewGame();
        if (b.first == "load_game") { screen_ = Screen::LoadGame; scroll_ = 0; refresh(); }
        if (b.first == "quit") return {MenuAction::Quit, ""};
        break;
    }
    return {};
}

void MainMenu::openNewGame(const std::string& initial) {
    screen_ = Screen::NewGame;
    float w = ui::screenW(), h = ui::screenH();
    nameInput_ = TextInput(centeredRect(w / 2, h / 2 - 20, 320, 44), initial);
}

MenuAction MainMenu::handleNewGame(const Event& e) {
    if (nameInput_) {
        nameInput_->handle(e);
        if (nameInput_->submitted) {
            nameInput_->submitted = false;
            std::string name = nameInput_->text;
            if (save::sanitizeName(name).size()) return submitNewGame(name);
        }
        if (nameInput_ && nameInput_->cancelled) {
            nameInput_.reset();
            screen_ = Screen::Root;
            return {};
        }
    }
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(startRect_, e.pos)) {
            std::string name = nameInput_ ? nameInput_->text : "";
            if (name.find_first_not_of(' ') != std::string::npos) return submitNewGame(name);
        } else if (inRect(backRect_, e.pos) || inRect(panelCloseRect(newGamePanel_), e.pos)) {
            nameInput_.reset();
            screen_ = Screen::Root;
        }
    }
    return {};
}

MenuAction MainMenu::submitNewGame(const std::string& raw) {
    if (!save::isValidName(raw)) {
        message_ = std::make_unique<MessageDialog>(INVALID_NAME_MESSAGE);
        return {};
    }
    std::string name = save::sanitizeName(raw);
    if (save::exists(name)) {
        pendingNewGame_ = name;
        confirmAction_ = Confirm::NewGame;
        confirm_ = std::make_unique<ConfirmDialog>("Overwrite save '" + name + "'?");
        nameInput_.reset();
        return {};
    }
    nameInput_.reset();
    screen_ = Screen::Root;
    return {MenuAction::NewGame, name};
}

MenuAction MainMenu::handleLoadGame(const Event& e) {
    if (e.type == EvType::KeyDown && e.key == KEY_ESCAPE) { screen_ = Screen::Root; return {}; }
    if (e.type == EvType::Wheel && viewport_.width > 0) {
        scroll_ = clampScroll(scroll_ - e.wheel * 40, contentHeight_, viewport_.height);
        return {};
    }
    if (e.type != EvType::MouseDown || e.button != MB_LEFT) return {};
    if (inRect(loadBackRect_, e.pos)) { screen_ = Screen::Root; return {}; }
    for (auto& r : rows_) {
        if (inRect(r.del, e.pos)) {
            deleteTarget_ = r.save;
            confirmAction_ = Confirm::Delete;
            confirm_ = std::make_unique<ConfirmDialog>("Delete save '" + r.save + "'?");
            return {};
        }
        if (inRect(r.edit, e.pos)) { openRename(r.save); return {}; }
        if (inRect(r.name, e.pos)) return {MenuAction::LoadGame, r.save};
    }
    return {};
}

void MainMenu::openRename(const std::string& name) {
    renameTarget_ = name;
    float w = ui::screenW(), h = ui::screenH();
    renameInput_ = TextInput(centeredRect(w / 2, h / 2, 320, 44), name);
}

MenuAction MainMenu::handleRename(const Event& e) {
    if (e.type == EvType::MouseDown && e.button == MB_LEFT &&
        (!inRect(renamePanel_, e.pos) || inRect(panelCloseRect(renamePanel_), e.pos) || inRect(renameCancelRect_, e.pos))) {
        renameInput_.reset();
        renameTarget_.clear();
        return {};
    }
    renameInput_->handle(e);
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(renameOkRect_, e.pos)) renameInput_->submitted = true;
    if (renameInput_->submitted) {
        renameInput_->submitted = false;
        std::string raw = renameInput_->text;
        std::string oldName = renameTarget_;
        std::string clean = save::sanitizeName(raw);
        if (clean.empty() || clean == oldName) {
            renameInput_.reset();
            renameTarget_.clear();
            return {};
        }
        if (!save::isValidName(raw)) {
            message_ = std::make_unique<MessageDialog>(INVALID_NAME_MESSAGE);
            return {};
        }
        renameInput_.reset();
        if (save::exists(clean)) {
            renameNewName_ = clean;
            confirmAction_ = Confirm::Rename;
            confirm_ = std::make_unique<ConfirmDialog>("Overwrite save '" + clean + "'?");
        } else {
            save::rename(oldName, clean);
            refresh();
            renameTarget_.clear();
        }
        return {};
    }
    if (renameInput_->cancelled) {
        renameInput_.reset();
        renameTarget_.clear();
    }
    return {};
}

void MainMenu::draw(float dt) {
    float w = ui::screenW(), h = ui::screenH();
    ClearBackground(hex("#987171"));
    ui::setHoverBlocked(confirm_ || message_ || renameInput_);

    if (screen_ == Screen::Root) {
        Vector2 tm = measure("Satis Factorio Program", 36, true);
        float titleY = h / 2 - 140;
        textCentered("Satis Factorio Program", w / 2, titleY, 36, ui::TEXT, true);
        float top = titleY + tm.y / 2 + 60;
        std::vector<std::pair<std::string, std::string>> labels;
        if (!lastOpened_.empty()) labels.push_back({"continue", "Continue (" + lastOpened_ + ")"});
        labels.push_back({"new_game", "New Game"});
        labels.push_back({"load_game", "Load Game"});
#ifndef __EMSCRIPTEN__
        labels.push_back({"quit", "Quit"});   // (in the browser you just close the tab)
#endif
        // a panel behind the buttons, like every other menu
        float ph = float(labels.size()) * 70 + 30;
        drawPanel(centeredRect(w / 2, top + ph / 2 - 15, 320, ph), "", false);
        rootButtons_.clear();
        for (size_t i = 0; i < labels.size(); ++i) {
            Rectangle r = centeredRect(w / 2, top + float(i) * 70 + 25, 260, 50);
            rootButtons_.push_back({labels[i].first, r});
            const std::string& id = labels[i].first;
            Color c = id == "continue" ? ui::BTN_OK : id == "quit" ? ui::BTN_DANGER : ui::BTN;
            drawButton(r, labels[i].second, c, 20);
        }
    } else if (screen_ == Screen::NewGame) {
        newGamePanel_ = centeredRect(w / 2, h / 2, 420, 200);
        drawPanel(newGamePanel_, "New Game");
        text("Save name:", newGamePanel_.x + 50, newGamePanel_.y + 40, 14, ui::TEXT_DIM);
        if (nameInput_) {
            nameInput_->update(dt);
            nameInput_->draw();
        }
        startRect_ = centeredRect(w / 2 - 70, h / 2 + 50, 120, 44);
        backRect_ = centeredRect(w / 2 + 70, h / 2 + 50, 120, 44);
        drawButton(startRect_, "Start", ui::BTN_OK);
        drawButton(backRect_, "Cancel", ui::BTN_DANGER);
    } else {
        // same look as the blueprint list
        Rectangle panel = centeredRect(w / 2, h / 2, 560, std::min(520.0f, h - 60));
        drawPanel(panel, "Load Game");
        loadBackRect_ = panelCloseRect(panel);   // the X goes back
        rows_.clear();
        if (saves_.empty()) {
            viewport_ = {};
            contentHeight_ = 0;
            textCentered("No saves yet", w / 2, panel.y + panel.height / 2, 20, ui::TEXT);
        } else {
            const float rowH = 48, spacing = 8, top = panel.y + ui::TITLE_H + 8;
            viewport_ = {panel.x, top, panel.width, panel.y + panel.height - 12 - top};
            contentHeight_ = float(saves_.size()) * (rowH + spacing);
            scroll_ = clampScroll(scroll_, contentHeight_, viewport_.height);
            ui::beginScissor({viewport_.x, viewport_.y, viewport_.width, viewport_.height});
            for (size_t i = 0; i < saves_.size(); ++i) {
                Rectangle row{panel.x + 14, top + float(i) * (rowH + spacing) - scroll_, panel.width - 28 - 10, rowH};
                if (row.y + row.height < viewport_.y || row.y > viewport_.y + viewport_.height) continue;
                fillRound(row, 8, hovered(row) ? Color{255, 255, 255, 175} : ui::ROW_BG);
                Rectangle del{row.x + row.width - 8 - 70, row.y + 8, 70, 32};
                Rectangle edit{del.x - 8 - 80, row.y + 8, 80, 32};
                Rectangle load{edit.x - 8 - 70, row.y + 8, 70, 32};
                std::string fitted = fitText(saves_[i], load.x - row.x - 20, 20);
                Vector2 m = measure(fitted, 20, true);
                text(fitted, row.x + 10, row.y + (rowH - m.y) / 2, 20, ui::TEXT, true);
                drawButton(load, "Load", ui::BTN_OK);       // the whole row loads, too
                drawButton(edit, "Rename", ui::BTN);
                drawButton(del, "Delete", ui::BTN_DANGER);
                rows_.push_back({row, edit, del, saves_[i]});
            }
            EndScissorMode();
            if (contentHeight_ > viewport_.height)
                drawScrollbar({panel.x + panel.width - 18, viewport_.y, 6, viewport_.height}, scroll_, contentHeight_, viewport_.height);
        }
    }

    ui::setHoverBlocked(confirm_ || message_);
    if (renameInput_) {
        drawModalShade();
        renamePanel_ = centeredRect(w / 2, h / 2 + 10, 420, 190);
        drawPanel(renamePanel_, "Rename save");
        renameInput_->update(dt);
        renameInput_->draw();
        renameOkRect_ = centeredRect(w / 2 - 70, renamePanel_.y + renamePanel_.height - 34, 120, 40);
        renameCancelRect_ = centeredRect(w / 2 + 70, renamePanel_.y + renamePanel_.height - 34, 120, 40);
        drawButton(renameOkRect_, "Rename", ui::BTN_OK);
        drawButton(renameCancelRect_, "Cancel", ui::BTN_DANGER);
    }
    ui::setHoverBlocked(false);
    if (confirm_) confirm_->draw();
    if (message_) message_->draw();
}
