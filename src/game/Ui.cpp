// game/Ui.cpp
#include "Ui.hpp"
#include "Assets.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
std::vector<Event> pollEvents() {
    std::vector<Event> out;
    Vector2 mouse = ui::mouse();   // UI coordinates

    // Key presses, in the order they happened.
    std::vector<int> pressed;
    for (int k = GetKeyPressed(); k != 0; k = GetKeyPressed()) pressed.push_back(k);
    // OS-style key repeat for editing keys.
    const int repeatKeys[] = {KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT};
    for (int k : repeatKeys)
        if (IsKeyPressedRepeat(k) && std::find(pressed.begin(), pressed.end(), k) == pressed.end()) pressed.push_back(k);
    for (int k : pressed) {
        Event e{EvType::KeyDown};
        e.key = k;
        e.pos = mouse;
        out.push_back(e);
    }
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
        Event e{EvType::Text};
        e.codepoint = c;
        e.pos = mouse;
        out.push_back(e);
    }
    const std::pair<int, int> buttons[] = {{MOUSE_BUTTON_LEFT, MB_LEFT}, {MOUSE_BUTTON_MIDDLE, MB_MIDDLE}, {MOUSE_BUTTON_RIGHT, MB_RIGHT}};
    for (auto& b : buttons) {
        if (IsMouseButtonPressed(b.first)) {
            Event e{EvType::MouseDown};
            e.button = b.second;
            e.pos = mouse;
            out.push_back(e);
        }
        if (IsMouseButtonReleased(b.first)) {
            Event e{EvType::MouseUp};
            e.button = b.second;
            e.pos = mouse;
            out.push_back(e);
        }
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Event e{EvType::Wheel};
        e.wheel = wheel;
        e.pos = mouse;
        out.push_back(e);
    }
    return out;
}

std::string costText(const ItemAmounts& cost) {
    std::string s;
    for (auto& c : cost) {
        if (c.second == 0) continue;
        if (!s.empty()) s += ", ";
        s += std::to_string(c.second) + " " + itemDef(c.first).name;
    }
    return s.empty() ? "free" : s;
}

std::string itemDeltaText(const ItemAmounts& delta) {
    std::string s;
    for (auto& c : delta) {
        if (c.second == 0) continue;
        if (!s.empty()) s += ", ";
        s += (c.second > 0 ? "+" : "-") + std::to_string(std::abs(c.second)) + " " + itemDef(c.first).name;
    }
    return s;
}

bool shiftDown() { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }
bool ctrlDown() { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
Color rgba(int r, int g, int b, int a) {
    return Color{(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
}

Color hex(const char* s, int a) {
    if (*s == '#') ++s;
    unsigned v = unsigned(std::strtoul(s, nullptr, 16));
    return rgba(int((v >> 16) & 255), int((v >> 8) & 255), int(v & 255), a);
}

bool inRect(Rectangle r, Vector2 p) { return CheckCollisionPointRec(p, r); }
bool inRect(Rectangle r, float x, float y) { return CheckCollisionPointRec(Vector2{x, y}, r); }
Rectangle centeredRect(float cx, float cy, float w, float h) { return {cx - w / 2, cy - h / 2, w, h}; }

static float roundness(Rectangle r, float radius) {
    float m = std::min(r.width, r.height);
    if (m <= 0) return 0;
    return std::clamp(radius * 2 / m, 0.0f, 1.0f);
}

void fillRound(Rectangle r, float radius, Color c) {
    if (radius <= 0) DrawRectangleRec(r, c);
    else DrawRectangleRounded(r, roundness(r, radius), 8, c);
}

void lineRound(Rectangle r, float radius, float thick, Color c) {
    DrawRectangleRoundedLinesEx(r, roundness(r, radius), 8, thick, c);
}

void lineRect(Rectangle r, float thick, Color c) { DrawRectangleLinesEx(r, thick, c); }

Vector2 measure(const std::string& s, int size, bool bold) {
    return MeasureTextEx(assets::font(size, bold), s.c_str(), float(size), 0);
}

void text(const std::string& s, float x, float y, int size, Color c, bool bold) {
    DrawTextEx(assets::font(size, bold), s.c_str(), Vector2{std::round(x), std::round(y)}, float(size), 0, c);
}

void textCentered(const std::string& s, float cx, float cy, int size, Color c, bool bold) {
    Vector2 m = measure(s, size, bold);
    text(s, cx - m.x / 2, cy - m.y / 2, size, c, bold);
}

void textTopRight(const std::string& s, float right, float top, int size, Color c, bool bold) {
    Vector2 m = measure(s, size, bold);
    text(s, right - m.x, top, size, c, bold);
}

std::string fitText(const std::string& s, float maxWidth, int size) {
    if (measure(s, size).x <= maxWidth) return s;
    std::string t = s;
    while (!t.empty() && measure(t + "...", size).x > maxWidth) t.pop_back();
    return t.empty() ? "..." : t + "...";
}

void drawTexture(const Texture2D& t, Rectangle dest, Color tint, float rotation) {
    Rectangle src{0, 0, float(t.width), float(t.height)};
    if (rotation == 0) {
        DrawTexturePro(t, src, dest, Vector2{0, 0}, 0, tint);
    } else {
        Rectangle d{dest.x + dest.width / 2, dest.y + dest.height / 2, dest.width, dest.height};
        DrawTexturePro(t, src, d, Vector2{dest.width / 2, dest.height / 2}, rotation, tint);
    }
}

void drawItemIcon(ItemId id, Rectangle dest, unsigned char alpha) {
    if (id == NO_ITEM) return;
    if (const Texture2D* t = assets::item(id)) {
        drawTexture(*t, dest, Color{255, 255, 255, alpha});
        return;
    }
    // No sprite yet (limestone, concrete): a simple labelled tile.
    Rectangle r{dest.x + dest.width * 0.1f, dest.y + dest.height * 0.1f, dest.width * 0.8f, dest.height * 0.8f};
    fillRound(r, r.width * 0.2f, Color{196, 190, 170, alpha});
    lineRound(r, r.width * 0.2f, 1, Color{90, 85, 70, alpha});
    std::string label = itemDef(id).name.substr(0, 2);
    int size = std::max(8, int(r.height * 0.55f));
    textCentered(label, r.x + r.width / 2, r.y + r.height / 2, size, Color{60, 55, 45, alpha}, true);
}

void drawSlotContents(const Slot& slot, Rectangle rect, int fontSize, float margin) {
    if (slot.empty()) return;
    float size = rect.width - margin * 2;
    drawItemIcon(slot.item, Rectangle{rect.x + margin, rect.y + margin, size, size});
    std::string n = std::to_string(slot.amount);
    Vector2 m = measure(n, fontSize);
    text(n, rect.x + rect.width - margin - m.x, rect.y + rect.height - margin - m.y, fontSize, BLACK);
}

void drawTooltip(const std::string& s, Vector2 at) {
    const int size = 16;
    const float pad = 8;
    Vector2 m = measure(s, size);
    float w = m.x + pad * 2, h = m.y + pad * 2;
    float x = at.x + 15, y = at.y + 15;
    if (x + w > ui::screenW()) x = ui::screenW() - w - 5;
    if (y + h > ui::screenH()) y = ui::screenH() - h - 5;
    fillRound({x, y, w, h}, 12, ui::PANEL_BG);
    text(s, x + pad, y + pad, size, BLACK);
}

void drawHoveredItemTooltip(const std::vector<HoverEntry>& entries) {
    Vector2 mouse = ui::mouse();
    for (auto& e : entries) {
        if (e.item == NO_ITEM || !inRect(e.rect, mouse)) continue;
        drawTooltip(itemDef(e.item).name, mouse);
        return;
    }
}

float clampScroll(float offset, float content, float viewport) {
    float maxOff = std::max(0.0f, content - viewport);
    return std::clamp(offset, 0.0f, maxOff);
}

void drawScrollbar(Rectangle track, float offset, float content, float viewport) {
    fillRound(track, track.width / 2, rgba(210, 210, 210));
    float maxOff = std::max(1.0f, content - viewport);
    float thumbH = std::max(24.0f, track.height * viewport / content);
    float thumbY = track.y + (track.height - thumbH) * (offset / maxOff);
    fillRound({track.x, thumbY, track.width, thumbH}, track.width / 2, rgba(140, 140, 140));
}

static bool g_hoverBlocked = false;
static float g_scale = 1.0f;

void ui::updateScale() {
    float s = std::min({1.0f, float(GetScreenWidth()) / DESIGN_W, float(GetScreenHeight()) / DESIGN_H});
    s *= BROWSER_FACTOR;   // a bit smaller in the browser (small Chromebook screen)
    // steps of 2.5 % (fewer font sizes to create while the window is resized)
    g_scale = std::max(0.25f, std::floor(s * 40.0f) / 40.0f);
}
float ui::scale() { return g_scale; }
float ui::screenW() { return float(GetScreenWidth()) / g_scale; }
float ui::screenH() { return float(GetScreenHeight()) / g_scale; }
Vector2 ui::mouse() { Vector2 m = GetMousePosition(); return {m.x / g_scale, m.y / g_scale}; }
Vector2 ui::toReal(Vector2 p) { return {p.x * g_scale, p.y * g_scale}; }
void ui::begin() {
    Camera2D c{};
    c.zoom = g_scale;
    BeginMode2D(c);
}
void ui::end() { EndMode2D(); }
void ui::beginScissor(Rectangle r) {
    BeginScissorMode(int(std::floor(r.x * g_scale)), int(std::floor(r.y * g_scale)),
                     int(std::ceil(r.width * g_scale)), int(std::ceil(r.height * g_scale)));
}
void ui::setHoverBlocked(bool blocked) { g_hoverBlocked = blocked; }

bool hovered(Rectangle r) { return !g_hoverBlocked && inRect(r, ui::mouse()); }

Color brighten(Color c, int a) {
    auto up = [a](unsigned char v) { return (unsigned char)std::min(255, int(v) + a); };
    return {up(c.r), up(c.g), up(c.b), c.a};
}

void drawButtonBase(Rectangle r, Color color, bool active) {
    if (active) color = brighten(color, 30);
    if (hovered(r)) color = brighten(color);
    fillRound(r, 8, color);
    if (active) lineRound(r, 8, 2, brighten(color, 45));
}

void drawButton(Rectangle r, const std::string& label, Color color, int fontSize, bool active) {
    drawButtonBase(r, color, active);
    textCentered(fitText(label, r.width - 12, fontSize), r.x + r.width / 2, r.y + r.height / 2, fontSize, WHITE);
}

Rectangle panelCloseRect(Rectangle p) { return {p.x + p.width - 10 - 28, p.y + 8, 28, 28}; }

void drawPanel(Rectangle r, const std::string& title, bool closeButton) {
    fillRound(r, ui::PANEL_RADIUS, ui::PANEL_BG);
    if (!title.empty()) {
        float room = r.width - (closeButton ? 2 * (28 + 14) : 24);   // keep clear of the X
        textCentered(fitText(title, room, ui::TITLE_SIZE), r.x + r.width / 2, r.y + ui::TITLE_H / 2, ui::TITLE_SIZE, ui::TEXT, true);
    }
    if (closeButton) {
        Rectangle c = panelCloseRect(r);
        fillRound(c, 6, hovered(c) ? brighten(ui::BTN_DANGER, 40) : ui::BTN_DANGER);
        textCentered("X", c.x + c.width / 2, c.y + c.height / 2, 18, WHITE, true);
    }
}

void drawModalShade() {
    DrawRectangle(0, 0, int(std::ceil(ui::screenW())), int(std::ceil(ui::screenH())), Color{0, 0, 0, 90});
}

// ---------------------------------------------------------------------------
// TextInput
// ---------------------------------------------------------------------------

TextInput::TextInput(Rectangle r, const std::string& initial) : rect(r), text(initial), cursor(int(initial.size())) {}

bool TextInput::fits(const std::string& s) const { return measure(s, fontSize).x <= rect.width - 16 - rightPad; }

int TextInput::indexAtX(float x) const {
    float click = x - (rect.x + 8);
    int best = 0;
    float bestDist = std::fabs(click);
    for (int i = 1; i <= int(text.size()); ++i) {
        float d = std::fabs(measure(text.substr(0, size_t(i)), fontSize).x - click);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void TextInput::handle(const Event& e) {
    if (e.type == EvType::MouseDown) {
        active = inRect(rect, e.pos);
        if (active) { cursor = indexAtX(e.pos.x); showCursor(); }
        return;
    }
    if (!active) return;
    if (e.type == EvType::Text) {
        if (e.codepoint < 32 || e.codepoint > 126) return; // printable ASCII only
        std::string cand = text.substr(0, size_t(cursor)) + char(e.codepoint) + text.substr(size_t(cursor));
        if (int(cand.size()) <= MAX_LENGTH && fits(cand)) {
            text = cand;
            ++cursor;
            showCursor();
        }
    } else if (e.type == EvType::KeyDown) {
        bool ctrl = ctrlDown();
        switch (e.key) {
            case KEY_BACKSPACE:
                if (ctrl) { text = text.substr(size_t(cursor)); cursor = 0; }
                else if (cursor > 0) { text.erase(size_t(cursor - 1), 1); --cursor; }
                showCursor();
                break;
            case KEY_DELETE:
                if (cursor < int(text.size())) text.erase(size_t(cursor), 1);
                showCursor();
                break;
            case KEY_LEFT: cursor = ctrl ? 0 : std::max(0, cursor - 1); showCursor(); break;
            case KEY_RIGHT: cursor = ctrl ? int(text.size()) : std::min(int(text.size()), cursor + 1); showCursor(); break;
            case KEY_ENTER: case KEY_KP_ENTER: submitted = true; break;
            case KEY_ESCAPE: cancelled = true; break;
            default: break;
        }
    }
}

void TextInput::update(float dt) {
    blink_ += dt;
    if (blink_ >= 0.3f) { blink_ = 0; cursorVisible_ = !cursorVisible_; }
}

void TextInput::draw() const {
    fillRound(rect, 8, WHITE);
    lineRound(rect, 8, 2, active ? ui::BTN : rgba(120, 120, 120));
    Vector2 m = measure(text.empty() ? "A" : text, fontSize);
    float ty = rect.y + (rect.height - m.y) / 2;
    ::text(text, rect.x + 8, ty, fontSize, BLACK);
    if (active && cursorVisible_) {
        float cx = rect.x + 8 + measure(text.substr(0, size_t(cursor)), fontSize).x;
        DrawLineEx({cx, rect.y + 6}, {cx, rect.y + rect.height - 6}, 2, BLACK);
    }
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------
DialogResult ConfirmDialog::handle(const Event& e) {
    if (e.type == EvType::MouseDown && e.button == MB_LEFT) {
        if (inRect(yes_, e.pos)) return DialogResult::Yes;
        if (inRect(no_, e.pos)) return DialogResult::No;
        if (box_.width > 0 && closeButton && inRect(panelCloseRect(box_), e.pos)) return DialogResult::Cancel;
        if (box_.width > 0 && !inRect(box_, e.pos)) return DialogResult::Cancel;
    } else if (e.type == EvType::KeyDown) {
        if (e.key == KEY_ENTER || e.key == KEY_KP_ENTER) return DialogResult::Yes;
        if (e.key == KEY_ESCAPE) return DialogResult::Cancel;
    }
    return DialogResult::None;
}

void ConfirmDialog::draw() {
    float w = ui::screenW(), h = ui::screenH();
    box_ = centeredRect(w / 2, h / 2, 400, 140);
    drawModalShade();
    drawPanel(box_, "", closeButton);
    float my = warning.empty() ? 45 : 34;
    textCentered(fitText(message, box_.width - (closeButton ? 90 : 32), 20), box_.x + box_.width / 2, box_.y + my, 20, ui::TEXT, true);
    if (!warning.empty())
        textCentered(fitText(warning, box_.width - 32, 16), box_.x + box_.width / 2, box_.y + 60, 16, rgba(170, 20, 20), true);
    yes_ = {box_.x + box_.width / 2 - 110, box_.y + box_.height - 56, 100, 36};
    no_ = {box_.x + box_.width / 2 + 10, box_.y + box_.height - 56, 100, 36};
    drawButton(yes_, "Yes", ui::BTN_OK);
    drawButton(no_, "No", ui::BTN_DANGER);
}

bool MessageDialog::handle(const Event& e) {
    if (e.type == EvType::MouseDown && e.button == MB_LEFT && inRect(ok_, e.pos)) return true;
    if (e.type == EvType::KeyDown && (e.key == KEY_ENTER || e.key == KEY_KP_ENTER || e.key == KEY_ESCAPE)) return true;
    return false;
}

void MessageDialog::draw() {
    float w = ui::screenW(), h = ui::screenH();
    Rectangle box = centeredRect(w / 2, h / 2, 550, 150);
    drawModalShade();
    drawPanel(box, "", false);
    textCentered(fitText(message, box.width - 32, 20), box.x + box.width / 2, box.y + 45, 20, ui::TEXT, true);
    ok_ = centeredRect(box.x + box.width / 2, box.y + box.height - 38, 100, 36);
    drawButton(ok_, "OK", ui::BTN);
}

// ---------------------------------------------------------------------------
// Expandable headlines
// ---------------------------------------------------------------------------
bool Foldout::handleClick(const Event& e, Rectangle header, const std::string& key) {
    if (e.type != EvType::MouseDown || e.button != MB_LEFT || !inRect(header, e.pos)) return false;
    toggle(key);
    return true;
}

void drawFoldArrow(Vector2 c, float size, bool open, Color col) {
    float h = size / 2;
    if (open)   // pointing down (vertices in the order raylib wants)
        DrawTriangle({c.x - h, c.y - h * 0.6f}, {c.x, c.y + h * 0.6f}, {c.x + h, c.y - h * 0.6f}, col);
    else        // pointing right
        DrawTriangle({c.x - h * 0.6f, c.y - h}, {c.x - h * 0.6f, c.y + h}, {c.x + h * 0.6f, c.y}, col);
}

float drawFoldHeader(Rectangle row, const std::string& title, bool open, const FoldHeaderStyle& st) {
    if (st.bg.a > 0) fillRound(row, st.radius, hovered(row) ? brighten(st.bg, 40) : st.bg);
    else if (hovered(row)) fillRound(row, st.radius, Color{255, 255, 255, 60});
    float arrow = std::max(10.0f, float(st.fontSize) * 0.6f);
    drawFoldArrow({row.x + 8 + arrow / 2, row.y + row.height / 2}, arrow, open, st.text);
    float tx = row.x + 8 + arrow + 10;
    Vector2 m = measure(title, st.fontSize, st.bold);
    text(title, tx, row.y + (row.height - m.y) / 2, st.fontSize, st.text, st.bold);
    return tx + m.x;
}

// ---------------------------------------------------------------------------
// Search box: a TextInput (same typing as everywhere else) + placeholder + x
// ---------------------------------------------------------------------------
SearchBox::SearchBox() {
    input.active = false;
    input.fontSize = 16;
}

void SearchBox::sync() {
    input.rect = rect;
    input.rightPad = rect.height;   // room for the x
}

bool SearchBox::handle(const Event& e) {
    sync();
    if (e.type == EvType::MouseDown) {
        if (!inRect(rect, e.pos)) { input.active = false; return false; }
        if (!input.text.empty() && inRect(clearRect(), e.pos)) {
            input.text.clear();
            input.cursor = 0;
            input.active = true;
            return true;
        }
        input.handle(e);   // focus + cursor where you clicked
        return true;
    }
    if (!input.active) return false;
    if (e.type == EvType::KeyDown || e.type == EvType::Text) {
        input.handle(e);
        if (input.submitted || input.cancelled) {   // Enter / ESC: stop typing, keep the text
            input.submitted = input.cancelled = false;
            input.active = false;
        }
        return true;   // every key belongs to the box while typing
    }
    return false;
}

bool SearchBox::matches(const std::string& s) const {
    if (input.text.empty()) return true;
    auto lower = [](std::string v) {
        for (char& c : v) c = char(std::tolower((unsigned char)c));
        return v;
    };
    return lower(s).find(lower(input.text)) != std::string::npos;
}

void SearchBox::draw(float dt) {
    sync();
    input.update(dt);
    input.draw();
    if (input.text.empty() && !input.active)
        ::text("Search...", rect.x + 8, rect.y + rect.height / 2 - 9, 16, rgba(140, 140, 140));
    if (!input.text.empty()) {   // x: clear
        Rectangle c = clearRect();
        Vector2 m{c.x + c.width / 2, c.y + c.height / 2};
        Color col = hovered(c) ? rgba(170, 20, 20) : rgba(110, 110, 110);
        DrawLineEx({m.x - 4, m.y - 4}, {m.x + 4, m.y + 4}, 2, col);
        DrawLineEx({m.x - 4, m.y + 4}, {m.x + 4, m.y - 4}, 2, col);
    }
}
