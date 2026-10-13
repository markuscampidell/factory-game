// game/MainMenu.hpp - title screen: Continue / New Game / Load Game / Quit.
#pragma once
#include "Ui.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct MenuAction {
    enum Kind { None, NewGame, LoadGame, LoadAutosave, Quit } kind = None;   // LoadAutosave: name = the normal save
    std::string name;
};

class MainMenu {
public:
    MainMenu();
    void refresh();
    // Coming back from a game: the start screen, nothing left open.
    void backToStart() { clearModal(); nameInput_.reset(); renameInput_.reset(); screen_ = Screen::Root; refresh(); }
    MenuAction handle(const Event& e);
    void draw(float dt);
    void showMessage(const std::string& msg) { message_ = std::make_unique<MessageDialog>(msg); }

private:
    enum class Screen { Root, NewGame, LoadGame } screen_ = Screen::Root;
    enum class Confirm { None, Delete, Rename, NewGame } confirmAction_ = Confirm::None;

    MenuAction handleRoot(const Event& e);
    MenuAction handleNewGame(const Event& e);
    MenuAction handleLoadGame(const Event& e);
    MenuAction handleRename(const Event& e);
    MenuAction submitNewGame(const std::string& name);
    void openNewGame(const std::string& initial = "");
    void openRename(const std::string& name);
    void clearModal();

    std::vector<std::string> saves_;
    std::vector<double> playTimes_;   // per save, read in refresh()
    std::string lastOpened_;

    std::optional<TextInput> nameInput_;
    std::optional<TextInput> renameInput_;
    std::unique_ptr<ConfirmDialog> confirm_;
    std::unique_ptr<MessageDialog> message_;
    std::string deleteTarget_, renameTarget_, renameNewName_, pendingNewGame_;

    // layout from the last draw (hit testing)
    std::vector<std::pair<std::string, Rectangle>> rootButtons_;
    Rectangle startRect_{}, backRect_{}, loadBackRect_{};
    Rectangle newGamePanel_{}, renamePanel_{}, renameOkRect_{}, renameCancelRect_{};
    struct Row { Rectangle name, edit, del, autosave; std::string save; };   // autosave: width 0 = none newer
    std::vector<Row> rows_;
    Rectangle viewport_{};
    float contentHeight_ = 0, scroll_ = 0;
};
