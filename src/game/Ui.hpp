// game/Ui.hpp - input events and small drawing/widget helpers.
#pragma once
#include "raylib.h"
#include "../core/Items.hpp"
#include "../core/Inventory.hpp"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Events (pygame-style, so UI code can react to one event at a time)
// ---------------------------------------------------------------------------
enum class EvType { KeyDown, MouseDown, MouseUp, Wheel, Text };
constexpr int MB_LEFT = 1, MB_MIDDLE = 2, MB_RIGHT = 3;

struct Event {
    EvType type;
    int key = 0;          // raylib KEY_*
    int button = 0;       // MB_LEFT / MB_MIDDLE / MB_RIGHT
    Vector2 pos{};        // mouse position
    float wheel = 0;      // + up
    int codepoint = 0;    // Text
};

std::vector<Event> pollEvents();
bool shiftDown();
bool ctrlDown();

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
Color rgba(int r, int g, int b, int a = 255);
Color hex(const char* s, int a = 255);   // "#RRGGBB"

bool inRect(Rectangle r, Vector2 p);
bool inRect(Rectangle r, float x, float y);
Rectangle centeredRect(float cx, float cy, float w, float h);

void fillRound(Rectangle r, float radius, Color c);
void lineRound(Rectangle r, float radius, float thick, Color c);
void lineRect(Rectangle r, float thick, Color c);

Vector2 measure(const std::string& text, int size, bool bold = false);
void text(const std::string& text, float x, float y, int size, Color c, bool bold = false);
void textCentered(const std::string& text, float cx, float cy, int size, Color c, bool bold = false);
void textTopRight(const std::string& s, float right, float top, int size, Color c, bool bold = false);
std::string fitText(const std::string& text, float maxWidth, int size);

// Item lists as text - shared by every build / delete / paste message.
// costText:      "10 Iron Ingot, 5 Copper Ingot"   ("free" if empty)
// itemDeltaText: "+1 Copper Ingot, -2 Iron Ingot"  (0 entries are skipped,
//                "" if nothing changed)
std::string costText(const ItemAmounts& cost);
std::string itemDeltaText(const ItemAmounts& delta);

void drawTexture(const Texture2D& t, Rectangle dest, Color tint = WHITE, float rotation = 0);
// Item icon (with fallback for items without a sprite) in `dest`.
void drawItemIcon(ItemId id, Rectangle dest, unsigned char alpha = 255);
// Stack in a slot: icon + amount in the bottom-right corner.
void drawSlotContents(const Slot& slot, Rectangle rect, int fontSize, float margin = 5);
// Rounded label with an item's name near the mouse.
void drawTooltip(const std::string& text, Vector2 at);

struct HoverEntry { Rectangle rect; ItemId item; };
// Draws the name tooltip for whichever entry the mouse is over.
void drawHoveredItemTooltip(const std::vector<HoverEntry>& entries);

// Scroll helpers (same behaviour as ui/scroll.py)
float clampScroll(float offset, float content, float viewport);
void drawScrollbar(Rectangle track, float offset, float content, float viewport);

// Everything clickable gets slightly brighter under the mouse. While a modal
// dialog is open, things behind it don't react (setHoverBlocked).
bool hovered(Rectangle r);
Color brighten(Color c, int amount = 28);
// Button background: `color`, brighter on hover; `active` (its panel is open,
// the selected recipe, ...) = brighter + white outline.
void drawButtonBase(Rectangle r, Color color, bool active = false);
void drawButton(Rectangle r, const std::string& label, Color color, int fontSize = 18, bool active = false);

// ---------------------------------------------------------------------------
// Shared look of every panel / dialog / menu (one place to change it)
// ---------------------------------------------------------------------------
namespace ui {
const Color PANEL_BG{202, 200, 228, 235};     // purple panel background
constexpr float PANEL_RADIUS = 18;
constexpr float TITLE_H = 44;                  // content starts below this
constexpr int TITLE_SIZE = 22;
constexpr int BUTTON_FONT = 18;
const Color TEXT{0, 0, 0, 255};
const Color TEXT_DIM{60, 60, 60, 255};          // secondary text / hints
const Color ROW_BG{255, 255, 255, 120};         // list rows inside a panel
const Color SELECTED{255, 165, 0, 255};         // selected row
const Color OUTLINE_HOVER{205, 205, 205, 255};   // slot border under the mouse
// choices inside a panel (recipes): panel colour, slightly darker outline
const Color CHOICE{202, 200, 228, 255};
const Color CHOICE_OUTLINE{150, 147, 185, 255};
const Color ACTIVE_SLOT{255, 195, 60, 255};          // selected hotbar slot (yellow)
const Color CHOICE_SELECTED = ACTIVE_SLOT;            // selected recipe: same yellow
const Color CHOICE_SELECTED_OUTLINE{205, 145, 25, 255};
// button colours
const Color BTN_OK{0, 150, 0, 255};             // confirm / start / save
const Color BTN{70, 70, 140, 255};              // neutral
const Color BTN_ACTIVE{100, 100, 190, 255};     // neutral, its panel is open
const Color BTN_DANGER{150, 0, 0, 255};         // cancel / delete / close
}

// Panel background + centred bold title (if any) + red X in the top-right
// corner (if `closeButton`). Click handling: inRect(panelCloseRect(r), p).
void drawPanel(Rectangle r, const std::string& title, bool closeButton = true);
Rectangle panelCloseRect(Rectangle panel);
namespace ui { void setHoverBlocked(bool blocked); }
// Darkens everything behind a modal dialog.
void drawModalShade();

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
class TextInput {
public:
    static constexpr int MAX_LENGTH = 32;
    TextInput() = default;
    TextInput(Rectangle r, const std::string& initial);
    void handle(const Event& e);
    void update(float dt);
    void draw() const;

    Rectangle rect{};
    std::string text;
    int cursor = 0;
    bool active = true;
    bool submitted = false;
    bool cancelled = false;

private:
    bool fits(const std::string& s) const;
    int indexAtX(float x) const;
    void showCursor() { cursorVisible_ = true; blink_ = 0; }
    bool cursorVisible_ = true;
    float blink_ = 0;
};

enum class DialogResult { None, Yes, No, Cancel };

class ConfirmDialog {
public:
    // closeButton: a red X that cancels (returns DialogResult::Cancel)
    explicit ConfirmDialog(std::string msg, bool closeButton = false)
        : message(std::move(msg)), closeButton(closeButton) {}
    DialogResult handle(const Event& e);
    void draw();
    std::string message;
    bool closeButton = false;
private:
    Rectangle box_{}, yes_{}, no_{};
};

class MessageDialog {
public:
    explicit MessageDialog(std::string msg) : message(std::move(msg)) {}
    bool handle(const Event& e); // true once acknowledged
    void draw();
    std::string message;
private:
    Rectangle ok_{};
};
