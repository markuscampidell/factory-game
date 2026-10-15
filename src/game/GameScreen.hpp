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
enum class Buildable { Smelter, Assembler, Belt, Splitter, Storage, Miner, WaterPump };

inline Buildable toBuildable(MachineType t) {
    switch (t) {
        case MachineType::Assembler: return Buildable::Assembler;
        case MachineType::Splitter: return Buildable::Splitter;
        case MachineType::Storage: return Buildable::Storage;
        case MachineType::Miner: return Buildable::Miner;
        case MachineType::WaterPump: return Buildable::WaterPump;
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
        case Buildable::WaterPump: return MachineType::WaterPump;
        default: return MachineType::Smelter;
    }
}

// Zoom limits. Below MAP_ZOOM the game switches to "map view": machines and
// belts become plain coloured boxes, the player can't move and nothing can
// be built or deleted (WASD pans the map instead).
constexpr float MIN_ZOOM = 0.01f;   // map view, zoomed all the way out (1 %)
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

// Map-view colours (GameDraw.cpp), shared by the map view, the minimap and
// the blueprint pictures in the blueprint list.
Color machineMapColor(MachineType t);
// A blueprint drawn like the map view, as big as fits in `square` (centred).
void drawBlueprintMap(const Blueprint& bp, Rectangle square);
// Side arrows of a smelter / assembler / miner (nothing for other machines):
// green pointing out on output sides (a miner: all four), red pointing in on
// input sides, at the middle tile of each side (the two middle tiles on an
// even-length side). `dest` = the machine in world pixels.
bool hasSideArrows(MachineType t);
void drawOutputSideArrows(MachineType t, Dir d, Rectangle dest, unsigned char alpha = 230);
// Map marker symbols: every sprite of the game (items, machines, belt).
struct MarkerSymbol { std::string id, name; };
const std::vector<MarkerSymbol>& markerSymbols();
void drawMarkerSymbol(const std::string& id, Rectangle dest);   // nothing for ""
std::string markerSymbolName(const std::string& id);

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
    uint64_t frameNo_ = 0;   // +1 every draw()
    // The held blueprint checked at the mouse - the ghost and the "Needs:" box
    // both need it; computed once per frame.
    struct BpCheckCache { uint64_t frame = ~0ull; Vec2i tl; bool replace = false; Builder::BlueprintCheck check; };
    BpCheckCache bpCheck_;
    const Builder::BlueprintCheck& heldBlueprintCheck(Vec2i tl, bool replace);
    std::string saveName_;

    // ---- zoom / map view ----
    bool mapMode() const { return cam_.zoom < MAP_ZOOM; }
    void zoomBy(float factor, Vector2 anchor);
    void setZoomTarget(float target, Vector2 anchor);
    void stepZoom(float dt);        // per frame: glide cam_.zoom to targetZoom_
    float targetZoom_ = 1.0f;       // where the smooth zoom is heading
    Vector2 zoomAnchor_{};          // real window pixels kept fixed while zooming
    bool zoomToPlayer_ = false;     // M / leaving the map: centre on the player
    void toggleMap();               // M / minimap click / zoom gate
    float lastNormalZoom_ = 1.0f;   // zoom to return to when leaving the map
    // Zoom gate: the wheel / + / - stop at the edge of the normal view (and
    // of the map view). ZOOM_GATE_STEPS more notches in the same direction,
    // each within ZOOM_GATE_TIME of the last, switch views. A bar in the
    // middle of the screen shows the progress.
    // Edge hold: if the wheel is still spinning when the zoom reaches the
    // edge (notches less than ZOOM_SPIN_PAUSE apart), the zoom stays at the
    // edge for ZOOM_EDGE_HOLD seconds (those notches do nothing); after that
    // the notches count towards the gate even while spinning. Notches after
    // a pause count straight away.
    static constexpr int ZOOM_GATE_STEPS = 10;
    static constexpr double ZOOM_GATE_TIME = 1.0;
    static constexpr double ZOOM_SPIN_PAUSE = 0.35;
    static constexpr double ZOOM_EDGE_HOLD = 0.25;
    double zoomLastNotch_ = -10;    // GetTime() of the last zoom notch of any kind
    double zoomEdgeSince_ = -1;     // when a spin reached the edge (-1: not holding)
    int zoomGate_ = 0;              // notches so far
    int zoomGateDir_ = 0;           // +1 towards the map, -1 back to normal
    double zoomGateLast_ = 0;       // GetTime() of the last notch
    float zoomGateShown_ = 0;       // drawn fill (0..1), glides after zoomGate_
    bool zoomGateActive() const;
    void drawZoomGate();
    void enterMapMode();
    // map view: drag with the left mouse button (when not copying / placing
    // anything) or the middle button (always) to move the map
    bool mapDragging_ = false;
    int mapDragButton_ = 0;          // raylib MOUSE_BUTTON_*
    Vector2 mapDragLast_{};          // real window pixels
    void updateMapDrag();
    void drawMapView();
    // Map view / minimap: one small texture per chunk, 1 pixel per tile -
    // terrain plus belts and machines. Rebuilt only when the chunk changes.
    struct MapChunkTex { Texture2D tex{}; uint32_t version = 0; bool explored = false; };
    std::unordered_map<Vec2i, MapChunkTex, Vec2iHash> mapChunks_;
    void clearMapChunks();

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
    int bpDeleteBookId_ = 0;           // > 0: the confirmation is for a book
    std::unique_ptr<ConfirmDialog> bpDeleteConfirm_;
    // Blueprint books: named groups in the list, opened / closed like the
    // Controls headlines. Blueprints are dragged onto a book (or out of it).
    std::vector<BlueprintBook> bpBooks_;
    Foldout bpFold_;                   // open books ("book:<id>")
    static std::string bookKey(int id) { return "book:" + std::to_string(id); }
    enum class BpRowKind { Book, Blueprint, EmptyBook };
    struct BpRow { BpRowKind kind; int index; int book; float y, h, indent; };  // y: from the list top
    std::vector<BpRow> bpListRows() const;
    Rectangle bpRowRect(const BpRow& row) const;          // on screen (with scroll)
    const BpRow* bpRowAt(const std::vector<BpRow>& rows, Vector2 p) const;
    int bpDropBook(Vector2 p) const;   // book id under p (0 = outside books), -1 = not over the list
    int bpDragIndex_ = -1;             // blueprint being dragged
    Vector2 bpDragFrom_{};
    bool bpDragging() const;           // dragged far enough to count as a drag
    void saveBlueprintList();
    // "New blueprint book" / "Rename book" dialog
    bool bpBookDialogOpen_ = false;
    int bpBookRenameId_ = 0;           // 0 = new book
    std::optional<TextInput> bpBookNameInput_;
    std::string bpBookError_;
    Rectangle bpBookDialogRect() const;
    void openBookDialog(int renameId);
    void submitBookDialog();
    bool bpBookDialogHandle(const Event& e);
    void drawBpBookDialog();
    // "Save blueprint" dialog shown after a Ctrl+B selection
    bool bpSaveOpen_ = false;
    // The same dialog also adds a blueprint straight into a book (the book's
    // "Add blueprint" button) and configures an existing one (rename /
    // reselect). Both are cleared by resetBuildState().
    int bpTargetBook_ = 0;             // > 0: the new blueprint goes into this book
    int bpConfigIndex_ = -1;           // >= 0: editing blueprints_[i] instead of adding
    std::string bpPendingName_;        // name typed before "Reselect"
    void openConfigure(int index);
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
    Foldout controlsFold_;           // which Controls headlines are open
    bool returnToMenu_ = false;
    // HUD buttons: [Menu] top right; [Alt] [Blueprints] bottom right
    bool showOutputs_ = true;      // Alt button / Alt key: recipe output icons on machines
    bool altClean_ = false;        // Alt is held and nothing else was pressed (Alt+Tab etc.)
    Rectangle blueprintButtonRect() const;
    Rectangle altButtonRect() const;
    // [Controls] next to [Menu]; [Copy] [Paste] [Delete area] above the
    // bottom right buttons; [Inventory] on the left edge, [Handcrafting] on
    // the right edge; the minimap in the bottom left corner
    Rectangle controlsButtonRect() const;
    Rectangle copyButtonRect() const;
    Rectangle pasteButtonRect() const;
    Rectangle cutButtonRect() const;
    Rectangle inventoryButtonRect() const;
    Rectangle craftButtonRect() const;
    Rectangle minimapRect() const;
    bool minimapVisible() const { return true; }   // also in map view (click: back)
    void toggleInventory();          // TAB / Inventory button
    void toggleCrafting();           // F / Handcrafting button
    void drawEdgeButtons();          // Inventory / Handcrafting (under the panels)
    void drawMinimap();
    // 1 pixel per tile terrain texture of an explored chunk (map view and
    // minimap); nullptr if the chunk hasn't been discovered
    const Texture2D* mapChunkTexture(int cx, int cy);   // nullptr: nothing to show (black)
    void drawPumpShore(Vec2i around);   // holding a water pump: the shore tiles where it fits
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
    // Ctrl + left click (and keep holding to sweep): take outputs / chest
    // contents from every machine and chest the mouse passes, each once.
    bool takeHolding_ = false;
    std::set<TileRef> takenThisHold_;
    void takeAt(Vector2 p);
    void updateTakeHold();
    // E (press or hold): pick up the items on the belts / splitters under
    // the player. One running "+N item" text per hold.
    bool pickUpHeld_ = false;
    ItemAmounts pickedUp_;          // during the current hold
    std::string pickUpText_;        // the text showing it
    void updatePickUp(bool held);
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

    // ---- undo (Ctrl + Z) ----
    // world_ records every change; once the left mouse button is up, what
    // was recorded becomes one undo step (a whole drag, a belt line, a paste
    // or an area delete). Builder::undo makes it item-exact.
    std::vector<UndoRecord> undoStack_;
    std::vector<UndoRecord> redoStack_;   // filled by undo, emptied by any new action
    static constexpr size_t UNDO_MAX = 50;
    void commitUndo();
    void undoLast();                      // Ctrl + Z (Ctrl + Y on German keyboards: same key)
    void redoLast();                      // Ctrl + R
    // shared by undo and redo: takes back `from.back()` and moves the
    // record that reverses it onto `to`
    void applyUndo(std::vector<UndoRecord>& from, std::vector<UndoRecord>& to, const char* verb);

    // ---- map markers (P, or the "Add map marker" button) ----
    std::vector<MapMarker> markers_;
    bool markerPlacing_ = false;       // holding a marker: left click places it
    Rectangle markerButtonRect() const;
    bool markerPlacingHandle(const Event& e);
    bool markerDialogOpen_ = false;
    int markerEdit_ = -1;              // index being edited, -1 = new marker
    Vec2i markerTile_;
    std::optional<TextInput> markerNameInput_;
    std::string markerSymbol_;         // chosen in the dialog ("" = none)
    SearchBox markerSearch_;           // filters the symbols
    float markerSymbolScroll_ = 0;
    std::vector<std::string> markerSymbolChoices() const;   // "" (none) + matching symbols
    Rectangle markerSymbolGrid() const;
    Rectangle markerSymbolCell(int k) const;                // k-th choice, with scroll
    Rectangle markerDialogRect() const;
    void openMarkerDialog(Vec2i tile, int editIndex);
    void submitMarkerDialog();
    bool markerDialogHandle(const Event& e);
    void drawMarkerDialog();
    int markerAt(Vector2 screenPos) const;    // index of the marker under the mouse, -1 if none
    Vector2 worldToUi(float wx, float wy) const;
    void drawMarkers();                // pins in the world / map view
    void drawMarkerPin(Vector2 at, float size, bool label, const std::string& name, const std::string& symbol);

    // ---- search boxes ----
    SearchBox craftSearch_, pickerSearch_, bpSearch_, machineSearch_;
    SearchBox* focusedSearch();
    std::vector<int> craftVisibleRecipes() const;   // indices into handcraftRecipes()
    std::vector<ItemId> pickerItems() const;
    Rectangle craftSearchRect() const;
    Rectangle craftListViewport() const;

    // ---- time played (saved) and the real clock, top right under Menu ----
    double playSeconds_ = 0;
    void drawClock();

    // ---- autosave: every AUTOSAVE_SECONDS of play the game is simply saved
    // under its own name (like pressing Save) ----
    static constexpr float AUTOSAVE_SECONDS = 300;
    float autosaveTimer_ = 0;

    // ---- small hints ----
    void drawPlacementHints();         // ghost colour reason, rotate / delete hints
    void drawOutputArrows();           // while holding a belt: where machines push

    // ---- notifications ----
    std::string toast_;
    float toastTime_ = 0;
    void notify(const std::string& s) { toast_ = s; toastTime_ = 2.5f; }
};
