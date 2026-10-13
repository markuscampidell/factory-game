// game/GameDraw.cpp - drawing the world, build previews and the HUD.
#include "GameScreen.hpp"
#include "Assets.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <unordered_set>

namespace {
const Color GROUND{110, 88, 60, 255};
// 30% opacity so the grid doesn't overpower the world
const Color GRID_COLOR{204, 204, 204, 77};
const Color CHUNK_COLOR{255, 140, 0, 77};
// faint full-screen tint while building / deleting
const Color BUILD_TINT{120, 255, 140, 22};
const Color DELETE_TINT{255, 120, 160, 26};

void drawFilterBadge(float cx, float cy, float size) {
    float h = size / 2;
    Color fill = hex("#FFD23F"), outline = hex("#402A00");
    // funnel: a wide triangle on top of a narrow stem
    Vector2 a{cx - h, cy - h}, b{cx + h, cy - h}, c{cx, cy + h * 0.2f};
    DrawTriangle(a, c, b, fill);
    Rectangle stem{cx - h * 0.3f, cy, h * 0.6f, h};
    DrawRectangleRec(stem, fill);
    DrawTriangleLines(a, c, b, outline);
    DrawRectangleLinesEx(stem, 1, outline);
}
} // namespace

// The world is drawn inside a raylib Camera2D (see draw()), so world
// coordinates are passed straight through.
Vector2 GameScreen::toScreen(float wx, float wy) const { return {wx, wy}; }

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void GameScreen::draw() {
    float dt = GetFrameTime();
    // what's under the mouse right now (delete target / hover highlight)
    validatePanels();
    hoveredDelete_ = mode_ == BuildMode::Deleting ? world_.refAt(mouseTile()) : 0;
    Vector2 mouse = ui::mouse();
    hovered_ = (!menuOpen_ && interactionAllowed(mouse)) ? objectAt(mouse) : 0;
    hoveredOre_.reset();
    if (!menuOpen_ && hovered_ == 0 && interactionAllowed(mouse)) {
        Vec2i t = mouseTile();
        if (world_.refAt(t) == 0 && world_.oreAt(t) != NO_ITEM) hoveredOre_ = t;
    }
    ui::setHoverBlocked(anyModalOpen());   // nothing behind a dialog lights up
    ClearBackground(GROUND);
    Camera2D c2d{};
    c2d.zoom = cam_.zoom;
    // at 100% snap to whole pixels so the pixel art stays crisp
    c2d.target = cam_.zoom == 1.0f ? Vector2{std::floor(cam_.x), std::floor(cam_.y)} : Vector2{cam_.x, cam_.y};
    BeginMode2D(c2d);
    if (mapMode()) {
        drawMapView();
        if (bpMode_ != BpMode::None) drawBlueprintMode();   // copy / paste / area delete / blueprints
    } else {
        drawTerrain();
        drawGrid();
        drawBelts();
        drawItems();
        drawMachines();
        drawPlayer();
        drawHoverHighlight();
        drawBuildMode();
        drawHandMining();
    }
    EndMode2D();
    if (mode_ == BuildMode::Building) DrawRectangle(0, 0, int(cam_.w), int(cam_.h), BUILD_TINT);
    else if (mode_ == BuildMode::Deleting) DrawRectangle(0, 0, int(cam_.w), int(cam_.h), DELETE_TINT);

    ui::begin();   // ---- everything below is UI (scaled down in small windows) ----
    drawFloatTexts(dt);

    drawMarkers();
    drawMinimap();
    drawEdgeButtons();     // under the panels
    drawMachinePanel();
    drawInventory();
    drawCraftPanel();
    drawStoragePanel();
    drawBeltFilterPanel();
    drawSplitterFilterPanel();
    drawCostInfo();
    drawBlueprintList();
    drawHotbar();
    drawMenuBar();

    // white text in map view so it stays readable over unexplored (black) areas
    Color hud = mapMode() ? WHITE : BLACK;
    text("Satis Factorio Program", 10, 10, 20, hud);
    text("Player position: x:" + std::to_string(int(player_.rect.cx())) + " y:" + std::to_string(int(player_.rect.cy())), 10, 35, 20, hud);
    text("FPS: " + std::to_string(int(fps_)), 10, 60, 20, hud);
    text("Zoom: " + std::to_string(int(std::lround(cam_.zoom * 100))) + "%" + (mapMode() ? "  (map view - P: place a marker)" : ""), 10, 85, 20, hud);

    if (markerPlacing_) {   // the marker you hold, and how to place it
        Vector2 m = ui::mouse();
        drawMarkerPin({m.x, m.y - 2}, 18, false, "", "");
        std::string hint = "Left click: place the marker here    Right click / ESC: cancel";
        Rectangle h = centeredRect(ui::screenW() / 2, 120, measure(hint, 16).x + 24, 28);
        fillRound(h, 10, ui::PANEL_BG);
        textCentered(hint, h.x + h.width / 2, h.y + 14, 16, ui::TEXT);
    }
    drawZoomGate();
    drawClock();
    drawHudTooltips();
    drawModals(dt);
    // notifications on top of everything ("Saved as ..." over the Game Menu)
    if (toastTime_ > 0 && !toast_.empty()) {
        Vector2 m = measure(toast_, 20);
        Rectangle r = centeredRect(ui::screenW() / 2, 70, m.x + 30, m.y + 16);
        unsigned char a = (unsigned char)(std::min(1.0f, toastTime_) * 230);
        fillRound(r, 10, Color{ui::PANEL_BG.r, ui::PANEL_BG.g, ui::PANEL_BG.b, a});
        textCentered(toast_, r.x + r.width / 2, r.y + r.height / 2, 20, Color{0, 0, 0, a}, true);
    }
    drawCursor();
    ui::end();
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
void GameScreen::drawTerrain() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    float icon = CELL_SIZE * 0.7f;
    for (int y = t; y < b; ++y)
        for (int x = l; x < r; ++x) {
            ItemId ore = world_.oreAt({x, y});
            if (ore == NO_ITEM) continue;
            Vector2 p = toScreen(float(x * CELL_SIZE), float(y * CELL_SIZE));
            drawItemIcon(ore, {p.x + (CELL_SIZE - icon) / 2, p.y + (CELL_SIZE - icon) / 2, icon, icon});
        }
}

void GameScreen::drawGrid() {
    if (mode_ == BuildMode::None) return;
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    float top = float(t * CELL_SIZE), bottom = float(b * CELL_SIZE);
    float left = float(l * CELL_SIZE), right = float(r * CELL_SIZE);
    float thin = 1.0f / cam_.zoom, thick = 2.0f / cam_.zoom;   // constant on screen
    for (int x = l; x <= r; ++x) {
        float wx = float(x * CELL_SIZE);
        if (floorMod(x, CHUNK_SIZE) == 0) DrawLineEx({wx, top}, {wx, bottom}, thick, CHUNK_COLOR);
        else DrawRectangleRec({wx, top, thin, bottom - top}, GRID_COLOR);
    }
    for (int y = t; y <= b; ++y) {
        float wy = float(y * CELL_SIZE);
        if (floorMod(y, CHUNK_SIZE) == 0) DrawLineEx({left, wy}, {right, wy}, thick, CHUNK_COLOR);
        else DrawRectangleRec({left, wy, right - left, thin}, GRID_COLOR);
    }
}

// ---------------------------------------------------------------------------
// Map view (zoomed far out): terrain as 1 pixel per tile, everything built as
// a plain coloured box.
// ---------------------------------------------------------------------------
static Color oreMapColor(uint8_t tileType) {
    switch (tileType) {
        case 1: return {140, 150, 170, 255};  // iron
        case 2: return {205, 120, 60, 255};   // copper
        case 3: return {35, 35, 40, 255};     // coal
        case 4: return {215, 205, 175, 255};  // limestone
        default: return GROUND;
    }
}

Color machineMapColor(MachineType t) {
    switch (t) {
        case MachineType::Smelter: return {200, 70, 50, 255};
        case MachineType::Assembler: return {70, 120, 210, 255};
        case MachineType::Splitter: return {60, 190, 90, 255};
        case MachineType::Storage: return {170, 120, 60, 255};
        case MachineType::Miner: return {230, 200, 40, 255};
        default: return {200, 200, 200, 255};
    }
}

const Texture2D* GameScreen::mapTerrainTexture(int cx, int cy) {
    if (!world_.isExplored(cx, cy)) return nullptr;
    Vec2i key{cx, cy};
    auto it = mapTerrain_.find(key);
    if (it == mapTerrain_.end()) {
        Image img = GenImageColor(CHUNK_SIZE, CHUNK_SIZE, GROUND);
        for (int y = 0; y < CHUNK_SIZE; ++y)
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                uint8_t tt = world_.tileType({cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y});
                if (tt) ImageDrawPixel(&img, x, y, oreMapColor(tt));
            }
        Texture2D tex = LoadTextureFromImage(img);
        UnloadImage(img);
        // clamp so the edge pixels never sample the opposite edge
        SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
        it = mapTerrain_.emplace(key, tex).first;
    }
    return &it->second;
}

void drawBlueprintMap(const Blueprint& bp, Rectangle sq) {
    fillRound(sq, 6, Color{GROUND.r, GROUND.g, GROUND.b, 255});
    if (bp.w <= 0 || bp.h <= 0) return;
    const float pad = 3;
    float tile = std::min((sq.width - 2 * pad) / float(bp.w), (sq.height - 2 * pad) / float(bp.h));
    float x0 = sq.x + (sq.width - tile * float(bp.w)) / 2, y0 = sq.y + (sq.height - tile * float(bp.h)) / 2;
    const Color beltColor{150, 150, 150, 255};
    for (const BlueprintBelt& b : bp.belts)
        DrawRectangleRec({x0 + float(b.off.x) * tile, y0 + float(b.off.y) * tile, tile, tile}, beltColor);
    for (const BlueprintMachine& m : bp.machines) {
        Vec2i sz = machineSize(m.type, m.dir);
        DrawRectangleRec({x0 + float(m.off.x) * tile, y0 + float(m.off.y) * tile, float(sz.x) * tile, float(sz.y) * tile},
                         machineMapColor(m.type));
    }
}

void GameScreen::drawMapView() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    int cx0 = floorDiv(l, CHUNK_SIZE), cy0 = floorDiv(t, CHUNK_SIZE);
    int cx1 = floorDiv(r, CHUNK_SIZE), cy1 = floorDiv(b, CHUNK_SIZE);
    const float chunkPx = float(CHUNK_SIZE * CELL_SIZE);

    // terrain: one tiny cached texture per explored chunk; chunks the player
    // hasn't walked to yet stay black (and are NOT generated here). One black
    // rectangle behind everything, so zoomed far out it isn't thousands.
    DrawRectangleRec({float(cx0) * chunkPx, float(cy0) * chunkPx, float(cx1 - cx0 + 1) * chunkPx, float(cy1 - cy0 + 1) * chunkPx}, BLACK);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            if (!world_.isExplored(cx, cy)) continue;
            // overlap neighbours by one screen pixel so no gap can appear
            // between chunks at fractional zoom levels
            float pad = 1.0f / cam_.zoom;
            drawTexture(*mapTerrainTexture(cx, cy), {cx * chunkPx, cy * chunkPx, chunkPx + pad, chunkPx + pad});
        }

    // belts and machines: walk only chunks that contain something
    const Color beltColor{150, 150, 150, 255};
    std::unordered_set<int> seen;
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            const Chunk* ch = world_.chunk(cx, cy, false);
            if (!ch || ch->occupants == 0) continue;
            for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; ++i) {
                TileRef ref = ch->refs[size_t(i)];
                if (ref > 0) {
                    float x = float((cx * CHUNK_SIZE + i % CHUNK_SIZE) * CELL_SIZE);
                    float y = float((cy * CHUNK_SIZE + i / CHUNK_SIZE) * CELL_SIZE);
                    DrawRectangleRec({x, y, CELL_SIZE, CELL_SIZE}, beltColor);
                } else if (ref < 0 && seen.insert(-ref - 1).second) {
                    const Machine& m = world_.machine(-ref - 1);
                    DrawRectangleRec({float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE),
                                      float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)}, machineMapColor(m.type));
                }
            }
        }

    // player: a box that stays visible however far out you zoom
    float s = std::max(player_.rect.w, 8.0f / cam_.zoom);
    DrawRectangleRec({player_.rect.cx() - s / 2, player_.rect.cy() - s / 2, s, s}, hex("#5F2D5D"));
    DrawRectangleLinesEx({player_.rect.cx() - s / 2, player_.rect.cy() - s / 2, s, s}, 1.5f / cam_.zoom, WHITE);
}

// ---------------------------------------------------------------------------
// Minimap (bottom left): the area around the player like the map view,
// MINIMAP_TILES tiles across.
// Hidden in map view; a click on it opens the map view (see menuBarHandle).
// ---------------------------------------------------------------------------
namespace {
constexpr float MINIMAP_SIZE = 170;
constexpr int MINIMAP_TILES = 256;   // 8 x 8 chunks
}

Rectangle GameScreen::minimapRect() const { return {10, ui::screenH() - 10 - MINIMAP_SIZE, MINIMAP_SIZE, MINIMAP_SIZE}; }

void GameScreen::drawMinimap() {
    if (!minimapVisible()) return;
    Rectangle r = minimapRect();
    const float k = MINIMAP_SIZE / float(MINIMAP_TILES * CELL_SIZE);   // minimap px per world px
    const float pcx = player_.rect.cx(), pcy = player_.rect.cy();
    auto mx = [&](float wx) { return r.x + r.width / 2 + (wx - pcx) * k; };
    auto my = [&](float wy) { return r.y + r.height / 2 + (wy - pcy) * k; };
    const float half = float(MINIMAP_TILES * CELL_SIZE) / 2;
    const int chunkWorld = CHUNK_SIZE * CELL_SIZE;
    int cx0 = floorDiv(int(std::floor(pcx - half)), chunkWorld), cx1 = floorDiv(int(std::floor(pcx + half)), chunkWorld);
    int cy0 = floorDiv(int(std::floor(pcy - half)), chunkWorld), cy1 = floorDiv(int(std::floor(pcy + half)), chunkWorld);
    const float chunkPx = float(chunkWorld) * k;

    fillRound({r.x - 4, r.y - 4, r.width + 8, r.height + 8}, 8, rgba(20, 20, 20));
    ui::beginScissor(r);
    DrawRectangleRec(r, BLACK);   // undiscovered
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            if (const Texture2D* tex = mapTerrainTexture(cx, cy))
                drawTexture(*tex, {mx(float(cx * chunkWorld)), my(float(cy * chunkWorld)), chunkPx + 0.5f, chunkPx + 0.5f});

    const Color beltColor{150, 150, 150, 255};
    const float tilePx = float(CELL_SIZE) * k;
    std::unordered_set<int> seen;
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx) {
            const Chunk* ch = world_.chunk(cx, cy, false);
            if (!ch || ch->occupants == 0) continue;
            for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; ++i) {
                TileRef ref = ch->refs[size_t(i)];
                if (ref > 0) {
                    float x = float((cx * CHUNK_SIZE + i % CHUNK_SIZE) * CELL_SIZE);
                    float y = float((cy * CHUNK_SIZE + i / CHUNK_SIZE) * CELL_SIZE);
                    DrawRectangleRec({mx(x), my(y), tilePx, tilePx}, beltColor);
                } else if (ref < 0 && seen.insert(-ref - 1).second) {
                    const Machine& m = world_.machine(-ref - 1);
                    DrawRectangleRec({mx(float(m.pos.x * CELL_SIZE)), my(float(m.pos.y * CELL_SIZE)), m.w * tilePx, m.h * tilePx},
                                     machineMapColor(m.type));
                }
            }
        }

    // map markers
    for (const MapMarker& mk : markers_) {
        Vector2 p{mx(float(mk.tile.x * CELL_SIZE + CELL_SIZE / 2)), my(float(mk.tile.y * CELL_SIZE + CELL_SIZE / 2))};
        if (inRect(r, p)) drawMarkerPin(p, 8, false, mk.name, mk.symbol);
    }
    // the player, always in the middle
    DrawRectangleRec(centeredRect(r.x + r.width / 2, r.y + r.height / 2, 6, 6), hex("#5F2D5D"));
    DrawRectangleLinesEx(centeredRect(r.x + r.width / 2, r.y + r.height / 2, 6, 6), 1, WHITE);
    EndScissorMode();
    if (hovered(r)) DrawRectangleLinesEx(r, 2, Color{255, 255, 255, 200});
}

void GameScreen::drawBelts() {
    world_.ensureTopology();
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    for (int y = t; y < b; ++y)
        for (int x = l; x < r; ++x) {
            int id = world_.beltIdAt({x, y});
            if (id < 0) continue;
            const Belt& belt = world_.belt(id);
            Vector2 p = toScreen(float(x * CELL_SIZE), float(y * CELL_SIZE));
            drawTexture(assets::belt(belt.incomingMask, belt.dir), {p.x, p.y, CELL_SIZE, CELL_SIZE});
            if (belt.filter.enabled) drawFilterBadge(p.x + CELL_SIZE - 8, p.y + 8, 10);
        }
}

void GameScreen::drawItems() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    const float size = CELL_SIZE * 0.5f;
    const float T = float(BELT_TICKS_PER_TILE);
    auto drawOnTile = [&](ItemId item, Vec2i tile, int progress, Dir from) {
        float p = progress < BELT_TICKS_PER_TILE ? (float(progress) + interp_) : T;
        p = std::min(p, T) / T;
        Vec2i v = dirVec(from);
        float ex = float(tile.x * CELL_SIZE + CELL_SIZE / 2), ey = float(tile.y * CELL_SIZE + CELL_SIZE / 2);
        float sx = ex - float(v.x * CELL_SIZE), sy = ey - float(v.y * CELL_SIZE);
        Vector2 s = toScreen(sx + (ex - sx) * p, sy + (ey - sy) * p);
        drawItemIcon(item, {s.x - size / 2, s.y - size / 2, size, size});
    };
    std::unordered_set<int> seen;
    for (int y = t - 1; y < b + 1; ++y)
        for (int x = l - 1; x < r + 1; ++x) {
            TileRef ref = world_.refAt({x, y});
            if (ref > 0) {
                const Belt& belt = world_.belt(ref - 1);
                if (belt.item != NO_ITEM) drawOnTile(belt.item, belt.pos, belt.progress, belt.itemFrom);
            } else if (ref < 0 && seen.insert(-ref - 1).second) {
                const Machine& m = world_.machine(-ref - 1);
                if (m.isSplitter() && m.item != NO_ITEM) drawOnTile(m.item, m.pos, m.progress, m.dir);
                for (const InputAnim& a : m.anims) {
                    float p = std::min(1.0f, (float(a.ticks) + interp_) / float(a.duration));
                    Vector2 s = toScreen(a.sx + (a.ex - a.sx) * p, a.sy + (a.ey - a.sy) * p);
                    drawItemIcon(a.item, {s.x - size / 2, s.y - size / 2, size, size});
                }
            }
        }
}

void GameScreen::drawMachines() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    std::unordered_set<int> seen;
    for (int y = t - 3; y < b + 3; ++y)
        for (int x = l - 3; x < r + 3; ++x) {
            int id = world_.machineIdAt({x, y});
            if (id < 0 || !seen.insert(id).second) continue;
            const Machine& m = world_.machine(id);
            Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
            Rectangle dest{p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)};
            assets::drawMachine(m.type, m.dir, dest);
            float cx = dest.x + dest.width / 2, cy = dest.y + dest.height / 2;

            if (m.isProducing() && m.recipe && !m.recipe->outputs.empty()) {
                if (!showOutputs_) continue;   // Alt button off
                // icons of what it makes, centred (native 32 px like Python)
                const float s = 32, spacing = 4;
                float total = float(m.recipe->outputs.size()) * s + spacing * float(m.recipe->outputs.size() - 1);
                float xx = cx - std::floor(total / 2);
                for (auto& o : m.recipe->outputs) {
                    drawItemIcon(o.first, {xx, cy - s / 2, s, s});
                    xx += s + spacing;
                }
            } else if (m.isStorage()) {
                if (!showOutputs_) continue;   // Alt mode off: no chest icons either
                ItemAmounts c = m.storage.contents();
                if (!c.empty()) {
                    auto best = std::max_element(c.begin(), c.end(), [](auto& a, auto& bb) { return a.second < bb.second; });
                    drawItemIcon(best->first, {cx - 16, cy - 16, 32, 32});
                }
            } else if (m.isSplitter()) {
                for (int i = 0; i < 3; ++i) {
                    if (!m.sideFilters[size_t(i)].enabled) continue;
                    Vec2i v = dirVec(m.sideDir(i));
                    drawFilterBadge(cx + v.x * (CELL_SIZE / 2.0f - 6), cy + v.y * (CELL_SIZE / 2.0f - 6), 9);
                }
            }
        }
}

void GameScreen::drawPlayer() {
    Vector2 p = toScreen(player_.rect.x, player_.rect.y);
    DrawRectangleRec({p.x, p.y, player_.rect.w, player_.rect.h}, hex("#5F2D5D"));
}

void GameScreen::drawHoverHighlight() {
    Color c{255, 255, 255, 90};
    if (hoveredOre_) {   // an ore tile you could mine by hand
        DrawRectangleRec({float(hoveredOre_->x * CELL_SIZE), float(hoveredOre_->y * CELL_SIZE), CELL_SIZE, CELL_SIZE}, c);
        return;
    }
    if (hovered_ == 0) return;
    if (hovered_ > 0) {
        const Belt& b = world_.belt(hovered_ - 1);
        Vector2 p = toScreen(float(b.pos.x * CELL_SIZE), float(b.pos.y * CELL_SIZE));
        DrawRectangleRec({p.x, p.y, CELL_SIZE, CELL_SIZE}, c);
    } else {
        const Machine& m = world_.machine(-hovered_ - 1);
        Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
        DrawRectangleRec({p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)}, c);
    }
}

// ---------------------------------------------------------------------------
// Build / delete previews
// ---------------------------------------------------------------------------
void GameScreen::drawGhostBelt(Vec2i tile, uint8_t mask, Dir out, Color overlay, bool hasOverlay) {
    Vector2 p = toScreen(float(tile.x * CELL_SIZE), float(tile.y * CELL_SIZE));
    Rectangle r{p.x, p.y, CELL_SIZE, CELL_SIZE};
    drawTexture(assets::belt(mask, out), r, Color{255, 255, 255, 160});
    if (hasOverlay) DrawRectangleRec(r, overlay);
}

void GameScreen::drawBuildMode() {
    if (bpMode_ != BpMode::None) { drawBlueprintMode(); return; }
    if (mode_ == BuildMode::Building) {
        if (selected_ == Buildable::Belt) { drawOutputArrows(); drawGhostBelts(); }
        else drawGhostMachine();
    } else if (mode_ == BuildMode::Deleting) {
        drawDeleteGhost();
    }
}

void GameScreen::drawGhostMachine() {
    MachineType type = toMachine(selected_);
    Dir dir = Dir(splitterRot_);   // splitters: 4 directions, a miner: standing / lying
    Vec2i size = machineSize(type, dir);
    Vec2i mt = mouseTile();
    Vec2i tl = Builder::machineTopLeft(type, mt, dir);
    PlaceStatus st = builder_.checkMachine(type, mt, Dir(splitterRot_), shiftDown());

    // Splitter: show how neighbouring belts would connect
    if (type == MachineType::Splitter && st != PlaceStatus::Blocked) {
        World::Overlay ov;
        ov.hasSplitter = true;
        ov.splitterPos = tl;
        ov.splitterDir = Dir(splitterRot_);
        for (int i = 0; i < 3; ++i) {
            Vec2i t = tl + dirVec(rotateCW(Dir(splitterRot_), i - 1));
            const Belt* b = world_.beltAt(t);
            if (!b) continue;
            uint8_t m = world_.computeIncoming(b->pos, b->dir, &ov);
            if (m != b->incomingMask) drawGhostBelt(b->pos, m, b->dir, {}, false);
        }
    }

    Vector2 p = toScreen(float(tl.x * CELL_SIZE), float(tl.y * CELL_SIZE));
    Rectangle dest{p.x, p.y, float(size.x * CELL_SIZE), float(size.y * CELL_SIZE)};
    assets::drawMachine(type, dir, dest, Color{255, 255, 255, 120});
    // Alt mode: what the new machine will make (its default recipe)
    if (showOutputs_) {
        auto preview = makeMachine(type, tl, Dir(splitterRot_));
        if (preview->isProducing() && preview->recipe && !preview->recipe->outputs.empty())
            drawItemIcon(preview->recipe->outputs[0].first, {dest.x + dest.width / 2 - 16, dest.y + dest.height / 2 - 16, 32, 32}, 160);
    }
    Color overlay{0, 0, 0, 0};
    switch (st) {
        case PlaceStatus::Blocked: overlay = {255, 0, 0, 80}; break;
        case PlaceStatus::NoSpace: overlay = {255, 165, 0, 80}; break;
        case PlaceStatus::NoFunds: overlay = {255, 255, 0, 80}; break;
        case PlaceStatus::NoOre: overlay = {128, 0, 128, 80}; break;
        case PlaceStatus::Undiscovered: overlay = {20, 20, 20, 170}; break;
        default: break;
    }
    if (overlay.a) DrawRectangleRec(dest, overlay);
}

void GameScreen::drawGhostBelts() {
    world_.ensureTopology();
    Vec2i mt = mouseTile();
    bool allow = shiftDown();
    const Color red{255, 0, 0, 120}, orange{255, 165, 0, 120}, yellow{255, 255, 0, 120};

    std::vector<BeltPlan> plan;
    std::vector<Color> colors;
    std::vector<bool> hasColor;
    if (!placingBelt_) {
        plan = builder_.planBelts(mt, mt, beltDir_);
        bool blocked = builder_.isDragTileBlocked(plan[0], mt, true, allow);
        if (blocked) { colors.push_back(red); hasColor.push_back(true); }
        else {
            PlaceStatus st = builder_.beltAffordability(plan);
            colors.push_back(st == PlaceStatus::NoSpace ? orange : yellow);
            hasColor.push_back(st != PlaceStatus::Ok);
        }
    } else {
        plan = builder_.planBelts(beltStart_, mt, beltDir_);
        if (builder_.anyDragTileBlocked(plan, beltStart_, allow)) {
            colors.assign(plan.size(), red);
            hasColor.assign(plan.size(), true);
        } else {
            auto ok = builder_.runningAffordability(plan);
            for (bool o : ok) { colors.push_back(yellow); hasColor.push_back(!o); }
        }
    }

    World::Overlay ov;
    for (auto& p : plan) ov.belts[p.pos] = p.dir;

    // existing neighbours whose sprite would change
    std::unordered_set<Vec2i, Vec2iHash> done;
    for (auto& p : plan)
        for (int i = 0; i < 4; ++i) {
            Vec2i n = p.pos + dirVec(Dir(i));
            if (ov.belts.count(n) || !done.insert(n).second) continue;
            const Belt* b = world_.beltAt(n);
            if (!b) continue;
            uint8_t m = world_.computeIncoming(b->pos, b->dir, &ov);
            if (m != b->incomingMask) drawGhostBelt(b->pos, m, b->dir, {}, false);
        }
    int l, t, r, bb;
    cam_.visibleTiles(l, t, r, bb);
    for (size_t i = 0; i < plan.size(); ++i) {
        const BeltPlan& p = plan[i];
        if (p.pos.x < l - 1 || p.pos.x > r || p.pos.y < t - 1 || p.pos.y > bb) continue;
        uint8_t m = world_.computeIncoming(p.pos, p.dir, &ov);
        drawGhostBelt(p.pos, m, p.dir, colors[i], hasColor[i]);
    }
}

void GameScreen::drawDeleteGhost() {
    if (hoveredDelete_ == 0) return;
    bool shift = shiftDown();
    std::vector<int> belts;
    bool canDelete;
    if (hoveredDelete_ > 0) {
        belts = shift ? builder_.connectedBelts(hoveredDelete_ - 1) : std::vector<int>{hoveredDelete_ - 1};
        canDelete = builder_.canAffordBeltDeletion(belts);
        // neighbours as they would look afterwards
        World::Overlay ov;
        for (int id : belts) ov.removed[world_.belt(id).pos] = true;
        std::unordered_set<Vec2i, Vec2iHash> done;
        for (int id : belts)
            for (int i = 0; i < 4; ++i) {
                Vec2i n = world_.belt(id).pos + dirVec(Dir(i));
                if (ov.removed.count(n) || !done.insert(n).second) continue;
                const Belt* b = world_.beltAt(n);
                if (!b) continue;
                uint8_t m = world_.computeIncoming(b->pos, b->dir, &ov);
                if (m != b->incomingMask) drawGhostBelt(b->pos, m, b->dir, {}, false);
            }
    } else {
        int id = -hoveredDelete_ - 1;
        canDelete = builder_.canAffordMachineDeletion(id);
        const Machine& m = world_.machine(id);
        if (m.isSplitter()) {
            World::Overlay ov;
            ov.ignoreMachine = id;
            for (int i = 0; i < 3; ++i) {
                const Belt* b = world_.beltAt(m.pos + dirVec(m.sideDir(i)));
                if (!b) continue;
                uint8_t mm = world_.computeIncoming(b->pos, b->dir, &ov);
                if (mm != b->incomingMask) drawGhostBelt(b->pos, mm, b->dir, {}, false);
            }
        }
    }
    Color c = canDelete ? Color{255, 0, 0, 100} : Color{255, 165, 0, 100};
    if (hoveredDelete_ > 0) {
        for (int id : belts) {
            Vec2i pos = world_.belt(id).pos;
            Vector2 p = toScreen(float(pos.x * CELL_SIZE), float(pos.y * CELL_SIZE));
            DrawRectangleRec({p.x, p.y, CELL_SIZE, CELL_SIZE}, c);
        }
    } else {
        const Machine& m = world_.machine(-hoveredDelete_ - 1);
        Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
        DrawRectangleRec({p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)}, c);
    }
}

void GameScreen::drawCursor() {
    Vector2 m = ui::mouse();
    Color c = mode_ == BuildMode::Building ? Color{255, 200, 50, 150}
            : mode_ == BuildMode::Deleting ? Color{255, 100, 150, 120}
                                           : Color{255, 255, 255, 100};
    DrawCircleV(m, 12, c);
    DrawRing(m, 10, 12, 0, 360, 32, Color{0, 0, 0, 200});
}

// ---------------------------------------------------------------------------
// Hand mining progress + floating texts
// ---------------------------------------------------------------------------
void GameScreen::drawHandMining() {
    if (!mining_) return;
    float x = float(miningTile_.x * CELL_SIZE), y = float(miningTile_.y * CELL_SIZE);
    float p = float(miningTicks_) / float(HAND_MINE_TICKS);
    Rectangle bar{x + 2, y + CELL_SIZE - 6, CELL_SIZE - 4, 4};
    DrawRectangleRec(bar, Color{0, 0, 0, 160});
    DrawRectangleRec({bar.x, bar.y, bar.width * p, bar.height}, Color{0, 230, 0, 255});
}

// Top right, under Menu / Controls: time played in this game and the clock.
void GameScreen::drawClock() {
    long t = long(playSeconds_);
    char played[48];
    std::snprintf(played, sizeof played, "Played: %ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    std::time_t now = std::time(nullptr);
    char clock[32] = "";
    if (std::tm* lt = std::localtime(&now)) std::strftime(clock, sizeof clock, "Time: %H:%M", lt);
    Color c = mapMode() ? WHITE : BLACK;   // like the text top left
    textTopRight(played, ui::screenW() - 10, 50, 18, c);
    textTopRight(clock, ui::screenW() - 10, 72, 18, c);
}

// The zoom gate's progress: a box in the middle of the screen.
void GameScreen::drawZoomGate() {
    if (!zoomGateActive()) { zoomGateShown_ = 0; return; }
    // the fill glides to the new step (about 90 % of the way in 0.15 s)
    float goal = float(zoomGate_) / float(ZOOM_GATE_STEPS);
    if (goal < zoomGateShown_) zoomGateShown_ = 0;   // a new run starts empty
    zoomGateShown_ += (goal - zoomGateShown_) * (1.0f - std::exp(-GetFrameTime() * 15.0f));
    std::string label = zoomGateDir_ > 0 ? "Zoom out for the map view" : "Zoom in for the normal view";
    float w = std::max(260.0f, measure(label, 18, true).x + 40);
    Rectangle box = centeredRect(ui::screenW() / 2, ui::screenH() / 2, w, 78);
    fillRound(box, 12, ui::PANEL_BG);
    textCentered(label, box.x + box.width / 2, box.y + 22, 18, ui::TEXT, true);
    // one continuous rounded bar
    Rectangle track{box.x + 20, box.y + 44, w - 40, 18};
    fillRound(track, 9, Color{255, 255, 255, 120});
    float fw = track.width * zoomGateShown_;
    if (fw >= 1) {
        // never narrower than it is high, so both ends stay round
        Rectangle fill{track.x, track.y, std::max(fw, track.height), track.height};
        fillRound(fill, track.height / 2, ui::ACTIVE_SLOT);
    }
    lineRound(track, 9, 1, ui::CHOICE_OUTLINE);
}

void GameScreen::drawFloatTexts(float dt) {
    const float LIFE = 1.6f;
    for (auto& f : floatTexts_) f.age += dt;
    floatTexts_.erase(std::remove_if(floatTexts_.begin(), floatTexts_.end(),
                                     [&](const FloatText& f) { return f.age >= LIFE; }),
                      floatTexts_.end());
    for (auto& f : floatTexts_) {
        // world -> screen (UI coordinates), drifting upwards and fading out
        float sx = (f.wx - cam_.x) * cam_.zoom / ui::scale();
        float sy = (f.wy - cam_.y) * cam_.zoom / ui::scale() - 12 - f.age * 25;
        unsigned char a = (unsigned char)(255 * std::min(1.0f, (LIFE - f.age) / 0.5f));
        Vector2 m = measure(f.text, 18, true);
        Rectangle bg{sx - m.x / 2 - 6, sy - m.y / 2 - 3, m.x + 12, m.y + 6};
        fillRound(bg, 6, Color{20, 20, 20, (unsigned char)(a * 0.7f)});
        textCentered(f.text, sx, sy, 18, Color{f.color.r, f.color.g, f.color.b, a}, true);
    }
}

// ---------------------------------------------------------------------------
// While holding a belt: on every side tile of the machines near the mouse a
// double arrow shows that a belt there can go in or out. Where a belt is
// already placed, only the matching arrow is left: green "out" (the belt
// points away - the machine pushes onto it) or blue "in" (the belt points
// into the machine and feeds it). A belt running along the side gets none.
// ---------------------------------------------------------------------------
void GameScreen::drawOutputArrows() {
    const int R = 12;   // tiles around the mouse
    Vec2i mt = mouseTile();
    std::unordered_set<int> seen;
    // a small triangle centred at (cx, cy) pointing along v
    auto arrow = [](float cx, float cy, Vector2 v, Color c) {
        Vector2 side{-v.y, v.x};
        const float len = CELL_SIZE * 0.13f, wid = CELL_SIZE * 0.16f;
        Vector2 tip{cx + v.x * len, cy + v.y * len};
        Vector2 a{cx - v.x * len + side.x * wid, cy - v.y * len + side.y * wid};
        Vector2 b{cx - v.x * len - side.x * wid, cy - v.y * len - side.y * wid};
        DrawTriangle(tip, a, b, c);   // raylib wants one winding; drawing both ways round
        DrawTriangle(tip, b, a, c);   // keeps it simple
    };
    const Color both{255, 255, 255, 140}, out{90, 230, 110, 220}, in{90, 170, 255, 220};
    for (int y = mt.y - R; y <= mt.y + R; ++y)
        for (int x = mt.x - R; x <= mt.x + R; ++x) {
            int id = world_.machineIdAt({x, y});
            if (id < 0 || !seen.insert(id).second) continue;
            const Machine& m = world_.machine(id);
            if (m.isSplitter()) continue;
            for (const OutputTile& ot : machineOutputTiles(m)) {
                TileRef r = world_.refAt(ot.tile);
                if (r < 0) continue;   // another machine there
                float cx = float(ot.tile.x * CELL_SIZE + CELL_SIZE / 2), cy = float(ot.tile.y * CELL_SIZE + CELL_SIZE / 2);
                Vector2 v{float(dirVec(ot.dir).x), float(dirVec(ot.dir).y)};   // away from the machine
                const float off = CELL_SIZE * 0.2f;
                if (r == 0) {   // free: in or out
                    arrow(cx + v.x * off, cy + v.y * off, v, both);
                    arrow(cx - v.x * off, cy - v.y * off, {-v.x, -v.y}, both);
                    continue;
                }
                Dir bd = world_.belt(r - 1).dir;
                if (bd == ot.dir) arrow(cx, cy, v, out);
                else if (bd == opposite(ot.dir)) arrow(cx, cy, {-v.x, -v.y}, in);
            }
        }
}

// ---------------------------------------------------------------------------
// Map markers: a pin + name, the same size on screen at any zoom
// ---------------------------------------------------------------------------
Vector2 GameScreen::worldToUi(float wx, float wy) const {
    return {(wx - cam_.x) * cam_.zoom / ui::scale(), (wy - cam_.y) * cam_.zoom / ui::scale()};
}

// ---- marker symbols: every sprite of the game ----
const std::vector<MarkerSymbol>& markerSymbols() {
    static std::vector<MarkerSymbol> list;
    if (list.empty()) {
        for (int i = 1; i <= itemCount(); ++i) list.push_back({"item:" + itemDef(ItemId(i)).id, itemDef(ItemId(i)).name});
        for (int t = 0; t < int(MachineType::Count); ++t)
            list.push_back({std::string("machine:") + machineSpec(MachineType(t)).saveType, machineSpec(MachineType(t)).name});
        list.push_back({"belt", "Belt"});
    }
    return list;
}

std::string markerSymbolName(const std::string& id) {
    for (auto& s : markerSymbols()) if (s.id == id) return s.name;
    return "";
}

void drawMarkerSymbol(const std::string& id, Rectangle d) {
    if (id.empty()) return;
    auto fit = [&](const Texture2D& t) {   // keep the sprite's shape, centred
        float sc = std::min(d.width / float(t.width), d.height / float(t.height));
        float w = float(t.width) * sc, h = float(t.height) * sc;
        drawTexture(t, {d.x + (d.width - w) / 2, d.y + (d.height - h) / 2, w, h});
    };
    if (id.rfind("item:", 0) == 0) { drawItemIcon(itemByStringId(id.substr(5)), d); return; }
    if (id == "belt") { fit(assets::belt(0, Dir::Right)); return; }
    if (id.rfind("machine:", 0) == 0) {
        MachineType t;
        if (machineTypeFromSave(id.substr(8), t))
            if (const Texture2D* tex = assets::machine(t)) fit(*tex);
    }
}

void GameScreen::drawMarkerPin(Vector2 at, float size, bool label, const std::string& name, const std::string& symbol) {
    // a round head on a short point; `at` is the tip
    const Color pin{255, 120, 40, 255};
    float r = size / 2;
    Vector2 head{at.x, at.y - size * 1.1f};
    DrawTriangle({head.x - r * 0.7f, head.y + r * 0.4f}, at, {head.x + r * 0.7f, head.y + r * 0.4f}, pin);
    DrawTriangle({head.x + r * 0.7f, head.y + r * 0.4f}, at, {head.x - r * 0.7f, head.y + r * 0.4f}, pin);
    DrawCircleV(head, r, pin);
    if (!symbol.empty() && size >= 14) {   // the symbol inside the head
        DrawCircleV(head, r * 0.8f, WHITE);
        drawMarkerSymbol(symbol, {head.x - r * 0.6f, head.y - r * 0.6f, r * 1.2f, r * 1.2f});
    } else {
        DrawCircleV(head, r * 0.4f, WHITE);
    }
    if (!label) return;
    // [symbol] name
    Vector2 m = measure(name, 16, true);
    float icon = symbol.empty() ? 0 : 20;
    float w = m.x + 12 + (icon > 0 ? icon + 4 : 0);
    Rectangle bg{head.x - w / 2, head.y - r - m.y - 8, w, m.y + 6};
    fillRound(bg, 6, Color{20, 20, 20, 180});
    if (icon > 0) drawMarkerSymbol(symbol, {bg.x + 5, bg.y + (bg.height - icon) / 2, icon, icon});
    text(name, bg.x + 6 + (icon > 0 ? icon + 4 : 0), bg.y + 3, 16, WHITE, true);
}

void GameScreen::drawMarkers() {
    for (const MapMarker& mk : markers_) {
        Vector2 p = worldToUi(float(mk.tile.x * CELL_SIZE + CELL_SIZE / 2), float(mk.tile.y * CELL_SIZE + CELL_SIZE / 2));
        if (p.x < -200 || p.y < -60 || p.x > ui::screenW() + 200 || p.y > ui::screenH() + 60) continue;
        drawMarkerPin(p, 18, mapMode(), mk.name, mk.symbol);   // names in the map view; normal view: hover shows it
    }
}

int GameScreen::markerAt(Vector2 sp) const {
    for (size_t i = markers_.size(); i-- > 0;) {
        const MapMarker& mk = markers_[i];
        Vector2 p = worldToUi(float(mk.tile.x * CELL_SIZE + CELL_SIZE / 2), float(mk.tile.y * CELL_SIZE + CELL_SIZE / 2));
        Rectangle hit{p.x - 11, p.y - 30, 22, 32};
        if (inRect(hit, sp)) return int(i);
    }
    return -1;
}

