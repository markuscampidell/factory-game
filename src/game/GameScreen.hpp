// game/GameScreen.hpp - the running game: input, fixed-tick update, drawing
// and every in-game panel. (Port of game_orchestrator.py plus the systems/
// and ui/ packages of the Python version.)
#pragma once
#include "Ui.hpp"
#include "../sim/World.hpp"
#include "../sim/Player.hpp"
#include "../sim/Build.hpp"
#include "../sim/Save.hpp"
#include <cmath>
#include <memory>
#include <optional>
#include <array>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

enum class BuildMode { None, Building, Deleting };
enum class Buildable { Smelter, Assembler, Belt, Splitter, Storage, Miner };

inline Buildable toBuildable(MachineType t) {
    switch (t) {
        case MachineType::Assembler: return Buildable::Assembler;
        case MachineType::Splitter: return Buildable::Splitter;
        case MachineType::Storage: return Buildable::Storage;
        case MachineType::Miner: return Buildable::Miner;
        default: return Buildable::Smelter;
    }
}

inline MachineType toMachine(Buildable b) {
    switch (b) {
        case Buildable::Smelter: return MachineType::Smelter;
        case Buildable::Assembler: return MachineType::Assembler;
        case Buildable::Splitter: return MachineType::Splitter;
        case Buildable::Storage: return MachineType::Storage;
        case Buildable::Miner: return MachineType::Miner;
        default: return MachineType::Smelter;
    }
}

// Zoom limits. Below MAP_ZOOM the game switches to "map view": machines and
// belts become plain coloured boxes, the player can't move and nothing can
// be built or deleted (WASD pans the map instead).
constexpr float MIN_ZOOM = 0.1f;
constexpr float MAX_ZOOM = 3.0f;
constexpr float MAP_ZOOM = 0.4f;

// x/y = world pixel at the top-left of the screen, w/h = screen size,
// zoom = screen pixels per world pixel.
struct View {
    float x = 0, y = 0, w = 1280, h = 720;
    float zoom = 1.0f;
    float margin = 350, smooth = 0.05f;
    float viewW() const { return w / zoom; }
    float viewH() const { return h / zoom; }
    void centerOn(float px, float py) { x = px - viewW() / 2; y = py - viewH() / 2; }
    void follow(const RectF& r);
    // Hard guarantee: moves the camera just enough that `r` is fully on
    // screen with a border around it, at any zoom level.
    void keepVisible(const RectF& r);
    void visibleTiles(int& l, int& t, int& rr, int& b) const;
    Vector2 screenToWorld(Vector2 p) const { return {x + p.x / zoom, y + p.y / zoom}; }
    // Zoom by `factor`, keeping the world point under `anchor` (screen) fixed.
    void zoomAt(float factor, Vector2 anchor);
};

class GameScreen {
public:
    GameScreen();
    ~GameScreen();
    GameScreen(const GameScreen&) = delete;
    GameScreen& operator=(const GameScreen&) = delete;

    void newGame(const std::string& name);
    bool loadGame(const std::string& name, std::string* error);

    void handle(const Event& e);
    void update(float dt);   // runs fixed ticks + per-frame logic
    void draw();

    bool returnToMenuRequested() const { return returnToMenu_; }
    void onMouseUp() { justPlaced_ = false; }

private:
    // ---- simulation ----
    void tick();
    void perFrame();
    World world_;
    Player player_;
    Builder builder_{world_, player_};
    View cam_;
    double accumulator_ = 0;
    float interp_ = 0;            // 0..1 between the last two ticks
    float fps_ = 0;
    std::string saveName_;

    // ---- zoom / map view ----
    bool mapMode() const { return cam_.zoom < MAP_ZOOM; }
    void zoomBy(float factor, Vector2 anchor);
    void toggleMap();               // M / Map button
    float lastNormalZoom_ = 1.0f;   // zoom to return to when leaving the map
    Rectangle mapButtonRect() const;
    void enterMapMode();
    void drawMapView();
    std::unordered_map<Vec2i, Texture2D, Vec2iHash> mapTerrain_; // 1 pixel per tile, per chunk

    // ---- build state ----
    BuildMode mode_ = BuildMode::None;
    Buildable selected_ = Buildable::Smelter;
    Dir beltDir_ = Dir::Right;
    int splitterRot_ = 0;
    bool placingBelt_ = false;
    Vec2i beltStart_;
    std::optional<Vec2i> lastDragged_;
    std::set<int> dragPlaced_;
    bool justPlaced_ = false;
    TileRef hoveredDelete_ = 0;
    TileRef hovered_ = 0;

    void resetBuildState();
    void resetRotation() { beltDir_ = Dir::Right; splitterRot_ = 0; }
    void select(Buildable b);
    void toggleDelete();
    void enterBuild();
    void rotateSelected(int steps);
    void cancelBuildOrDelete();
    Vec2i mouseTile() const;
    Vec2i screenToTile(Vector2 p) const;

    void handlePlacement(const Event& e);
    void updateDrag();
    void tryDeleteAt(Vec2i tile);
    void tryPlaceMachineAt(Vec2i tile);
    bool mouseOverOpenPanel(Vector2 p) const;
    bool interactionAllowed(Vector2 p) const;
    TileRef objectAt(Vector2 p) const;
    void worldClick(const Event& e);
    void pickKey(const Event& e);
    void handleKeys(const Event& e);
    void handleMouse(const Event& e);

    // ---- panels ----
    bool invOpen_ = false;
    bool craftOpen_ = false;
    float craftScroll_ = 0;
    int machinePanel_ = -1;       // machine id (producing machine)
    int storagePanel_ = -1;
    int splitterPanel_ = -1;
    int beltPanel_ = -1;          // belt id
    Vec2i beltPanelPos_;
    Vec2i machinePanelPos_, storagePanelPos_, splitterPanelPos_;
    struct PickerTarget { bool splitter; int side; int slot; };
    std::optional<PickerTarget> picker_;

    // Panel rules (one place for all of them):
    //  * At most two panels: the Inventory, alone or together with a
    //    machine, storage or Handcrafting panel (those always bring it).
    //    Blueprints, belt filter and splitter filter are always alone.
    //  * Opening a panel closes whatever else is open. Closing any panel of
    //    a pair (X, ESC, TAB, F) closes both.
    //  * Open panels sit side by side (Inventory left), tops aligned, and the
    //    rectangle around them is centred on the screen.
    //  * Starting to build (hotbar, middle click, Ctrl+C/X/B) or opening the
    //    Game Menu closes all panels.
    enum class Panel { Inventory, Craft, Machine, Storage, Belt, Splitter, Blueprints };
    bool isOpen(Panel p) const;
    Rectangle panelRect(Panel p) const;
    Vector2 inventorySize() const;
    // Inventory + partner side by side, centred as a group; `self` = the
    // rect asked for (left = inventory, right = partner).
    Rectangle pairRect(Vector2 partnerSize, bool left) const;
    Rectangle centeredPanel(Vector2 size) const;
    void drawRecipeInfoNearMouse(const Recipe& rc);
    void closePanel(Panel p);
    void makeRoomFor(Panel p);   // call right before opening p
    bool anyPanelOpen() const;
    void closeAllPanels();
    void closeIfClickedOutside(Vector2 p);
    bool panelCloseClick(const Event& e);   // the red X of the in-game panels
    bool closeTopPanel();                   // ESC: closes only the panel on top
    void validatePanels();
    ItemFilter* pickerFilter();

    Rectangle inventoryRect() const;
    Rectangle craftRect() const;
    Rectangle machineRect() const;
    Rectangle storageRect() const;
    Rectangle beltFilterRect() const;
    Rectangle splitterFilterRect() const;

    void inventoryHandle(const Event& e);
    void machinePanelHandle(const Event& e);
    void storagePanelHandle(const Event& e);
    void beltFilterHandle(const Event& e);
    void splitterFilterHandle(const Event& e);
    void craftHandleClick(const Event& e);
    bool filterPanelClick(Vector2 topLeft, ItemFilter& f, bool splitter, int side, const Event& e, bool withLabel);
    bool pickerClick(Vector2 topLeft, const Event& e);

    void drawInventory();
    void drawMachinePanel();
    void drawStoragePanel();
    void drawBeltFilterPanel();
    void drawSplitterFilterPanel();
    void drawCraftPanel();
    void drawRecipePanel(const Recipe& r, Rectangle rect);
    void drawFilterPanel(Vector2 topLeft, const ItemFilter& f, const char* label);
    void drawPicker(Vector2 topLeft);
    Color arrowColor_{50, 50, 50, 255};

    // ---- multi copy / paste and blueprints ----
    // Ctrl+C: drag a rectangle -> clipboard -> place it. Ctrl+V: place the
    // clipboard again. Ctrl+B: same, but saved in the blueprint list (B).
    // Cutting = Ctrl+X: drag a rectangle, everything inside is deleted.
    enum class BpMode { None, Selecting, Placing, Cutting };
    BpMode bpMode_ = BpMode::None;
    bool bpForList_ = false;           // the selection becomes a saved blueprint
    bool bpDragging_ = false;
    Vec2i bpDragStart_;
    Blueprint clipboard_;
    Blueprint placing_;                // what is attached to the mouse right now
    std::vector<Blueprint> blueprints_;
    bool bpListOpen_ = false;
    float bpListScroll_ = 0;
    int bpDeleteIndex_ = -1;
    std::unique_ptr<ConfirmDialog> bpDeleteConfirm_;
    // "Save blueprint" dialog shown after a Ctrl+B selection
    bool bpSaveOpen_ = false;
    Blueprint bpPending_;
    Vec2i bpPendingA_, bpPendingB_;
    std::optional<TextInput> bpNameInput_;
    std::string bpSaveError_;
    Rectangle bpSaveRect() const;
    bool bpSaveHandle(const Event& e);
    void drawBpSaveDialog();
    void drawRecipeButton(Rectangle r, const Recipe& rc, bool selected, std::vector<HoverEntry>& hover);
    void savePendingBlueprint();
    bool blueprintHandle(const Event& e);
    void startSelecting(bool forList);
    void startCutting();
    void finishCut(Vec2i end);
    void startPlacing(const Blueprint& bp);
    void finishSelection(Vec2i end);
    void placeHeldBlueprint(Vector2 mouse);
    bool overUi(Vector2 p) const;
    Rectangle bpListRect() const;
    void openBlueprintList();
    void drawBlueprintMode();
    void drawBlueprintList();

    // ---- hotbar ----
    bool hotbarHandle(const Event& e);
    void hotbarActivate(int slot);
    void drawHotbar();
    std::vector<Rectangle> hotbarRects() const;
    std::string hotbarTooltip(Vector2 p) const;   // "" if not over a slot

    // ---- game menu (top right) ----
    bool menuOpen_ = false;
    std::unique_ptr<ConfirmDialog> exitConfirm_;
    bool saveAsOpen_ = false;
    std::optional<TextInput> saveAsInput_;
    std::unique_ptr<ConfirmDialog> saveAsConfirm_;
    std::unique_ptr<MessageDialog> saveAsMessage_;
    std::string pendingSaveAs_;
    bool controlsOpen_ = false;
    float controlsScroll_ = 0;
    bool returnToMenu_ = false;
    // HUD buttons: [Menu] top right; [Alt] [Blueprints] bottom right
    bool showOutputs_ = true;      // Alt button / Alt key: recipe output icons on machines
    bool altClean_ = false;        // Alt is held and nothing else was pressed (Alt+Tab etc.)
    Rectangle blueprintButtonRect() const;
    Rectangle altButtonRect() const;
    bool overHudButtons(Vector2 p) const;
    std::string hudButtonTooltip(Vector2 p) const;   // "" if not over a button
    void drawHudTooltips();
    bool menuBarHandle(const Event& e);
    void drawMenuBar();              // HUD buttons
    bool anyModalOpen() const;
    void drawModals(float dt);       // game menu, dialogs - drawn last
    void openSaveAs(const std::string& initial);
    void submitSaveAs(const std::string& name);
    void saveNow(const std::string& name);
    bool controlsHandle(const Event& e);
    void drawControls();

    // ---- world drawing ----
    void drawTerrain();
    void drawGrid();
    void drawBelts();
    void drawItems();
    void drawMachines();
    void drawPlayer();
    void drawHoverHighlight();
    void drawBuildMode();
    void drawGhostMachine();
    void drawGhostBelts();
    void drawDeleteGhost();
    void drawCursor();
    void drawGhostBelt(Vec2i tile, uint8_t mask, Dir out, Color overlay, bool hasOverlay);
    Vector2 toScreen(float wx, float wy) const;

    // ---- hand mining (hold right mouse on an ore tile) ----
    Vec2i miningTile_;
    int miningTicks_ = 0;          // progress towards the next ore
    bool mining_ = false;
    int fullMessageCooldown_ = 0;
    void updateHandMining();
    void drawHandMining();

    // ---- copy / paste (shift + right click = copy, shift + left = paste) ----
    // Three independent clipboards: copying a belt filter doesn't forget the
    // copied recipe, and so on.
    const Recipe* copiedRecipe_ = nullptr;
    std::optional<ItemFilter> copiedBeltFilter_;
    std::optional<std::array<ItemFilter, 3>> copiedSplitterFilters_;
    bool pasteHolding_ = false;          // shift + left held: paste onto everything swept over
    std::set<TileRef> pastedThisHold_;   // objects already handled during this hold
    bool copyPasteClick(const Event& e);
    bool copyPasteAllowed(Vector2 p) const;
    void copyAt(Vector2 p);
    void pasteAt(Vector2 p);
    void updatePasteHold();
    void pasteSuccess(float wx, float wy, const std::string& kind, const std::string& what);
    std::optional<Vec2i> hoveredOre_;

    // ---- floating texts in the world ("Copied recipe", "Inventory full") ----
    struct FloatText { float wx, wy; std::string text; Color color; float age = 0; };
    std::vector<FloatText> floatTexts_;
    void floatText(float wx, float wy, const std::string& s, Color c);
    void drawFloatTexts(float dt);
    // "+1 Copper Ingot, -2 Iron Ingot" after building / deleting / pasting.
    // Green when you got items back, orange when you paid, white if both.
    void showItemDelta(float wx, float wy, const ItemAmounts& delta);
    // "Needs: ..." box while holding a ghost machine / belt / blueprint.
    void drawCostInfo();

    // ---- middle click: pick up a machine / belt / splitter to build it ----
    // Only the type and direction; recipe and filters stay at their defaults.
    // Not the Ctrl+C clipboard.
    bool pipetteClick(const Event& e);

    // ---- notifications ----
    std::string toast_;
    float toastTime_ = 0;
    void notify(const std::string& s) { toast_ = s; toastTime_ = 2.5f; }
};
