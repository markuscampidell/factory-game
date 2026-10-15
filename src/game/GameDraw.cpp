// game/GameDraw.cpp - drawing the world, build previews and the HUD.
#include "GameScreen.hpp"
#include "Assets.hpp"
#include "rlgl.h"   // rlSetBlendFactors (drawHiddenUnderSprite) - after raylib.h
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <unordered_set>

namespace {
const Color GROUND{110, 88, 60, 255};
const Color WATER{52, 110, 170, 255};
const Color LANDFILL{96, 76, 52, 255};   // filled ground: a bit darker than GROUND
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
    ++frameNo_;
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
    cam2d_ = c2d;   // (drawHiddenUnderSprite leaves and re-enters it)
    BeginMode2D(c2d);
    if (mapMode()) {
        drawMapView();
        if (bpMode_ != BpMode::None) drawBlueprintMode();   // copy / paste / area delete / blueprints
    } else {
        drawTerrain();
        drawGrid();
        drawBelts();
        drawItems();
        drawBridges();   // one level up - but under machines (they never overlap), so items
                         // coming out of / going into a machine slide under its sprite
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
    text(std::string("Layer: ") + layerName(layer_) + "  (L)", 10, 110, 20, layer_ == Layers::All ? hud : Color{170, 20, 20, 255});

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
        Vector2 m = measure(toast_, 20, true);   // (drawn bold)
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
            if (world_.isWater({x, y})) {   // one plain colour
                DrawRectangleRec({float(x * CELL_SIZE), float(y * CELL_SIZE), CELL_SIZE, CELL_SIZE}, WATER);
                continue;
            }
            if (world_.isLandfill({x, y})) {
                DrawRectangleRec({float(x * CELL_SIZE), float(y * CELL_SIZE), CELL_SIZE, CELL_SIZE}, LANDFILL);
                continue;   // (no ore under landfill)
            }
            ItemId ore = world_.oreAt({x, y});
            if (ore == NO_ITEM) continue;
            Vector2 p = toScreen(float(x * CELL_SIZE), float(y * CELL_SIZE));
            drawItemIcon(ore, {p.x + (CELL_SIZE - icon) / 2, p.y + (CELL_SIZE - icon) / 2, icon, icon});
        }
}

// One tile of bare terrain (ground / water / landfill / ore) with its grid
// lines - drawn over things to hide them.
void GameScreen::drawTerrainTile(Vec2i t) {
    Rectangle r{float(t.x * CELL_SIZE), float(t.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
    if (world_.isWater(t)) DrawRectangleRec(r, WATER);
    else if (world_.isLandfill(t)) DrawRectangleRec(r, LANDFILL);
    else {
        DrawRectangleRec(r, GROUND);
        if (ItemId ore = world_.oreAt(t); ore != NO_ITEM) {
            float icon = CELL_SIZE * 0.7f;
            drawItemIcon(ore, {r.x + (CELL_SIZE - icon) / 2, r.y + (CELL_SIZE - icon) / 2, icon, icon});
        }
    }
    if (mode_ != BuildMode::None) {
        float thin = 1.0f / cam_.zoom;
        DrawRectangleRec({r.x, r.y, thin, r.height}, GRID_COLOR);
        DrawRectangleRec({r.x, r.y, r.width, thin}, GRID_COLOR);
    }
}

// The item on a belt / bridge, sliding in from where it came.
void GameScreen::drawBeltItem(const Belt& b, unsigned char alpha) {
    if (b.item == NO_ITEM) return;
    const float size = CELL_SIZE * 0.5f, T = float(BELT_TICKS_PER_TILE);
    float p = b.progress < BELT_TICKS_PER_TILE ? (float(b.progress) + interp_) : T;
    p = std::min(p, T) / T;
    Vec2i v = dirVec(b.itemFrom);
    float ex = float(b.pos.x * CELL_SIZE + CELL_SIZE / 2), ey = float(b.pos.y * CELL_SIZE + CELL_SIZE / 2);
    float sx = ex - float(v.x * CELL_SIZE), sy = ey - float(v.y * CELL_SIZE);
    drawItemIcon(b.item, {sx + (ex - sx) * p - size / 2, sy + (ey - sy) * p - size / 2, size, size}, alpha);
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
        case 5: return WATER;                 // water
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
        case MachineType::WaterPump: return {120, 215, 235, 255};
        default: return {200, 200, 200, 255};
    }
}

const Texture2D* GameScreen::mapChunkTexture(int cx, int cy) {
    bool explored = world_.isExplored(cx, cy);
    const Chunk* ch = world_.chunk(cx, cy, false);
    bool built = ch && ch->occupants > 0;
    if (!explored && !built) return nullptr;
    uint32_t version = ch ? ch->version : 0;
    Vec2i key{cx, cy};
    auto it = mapChunks_.find(key);
    if (it != mapChunks_.end() && it->second.version == version && it->second.explored == explored) return &it->second.tex;

    // terrain (black where unexplored), belts grey, machines in their colour, bridges on top
    const Color beltColor{150, 150, 150, 255}, bridgeColor{210, 185, 125, 255};
    const bool anyBridges = !world_.bridges().empty();
    Color px[CHUNK_SIZE * CHUNK_SIZE];
    for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            int i = y * CHUNK_SIZE + x;
            TileRef r = ch ? ch->refs[size_t(i)] : 0;
            if (r > 0) px[i] = beltColor;
            else if (r < 0) px[i] = machineMapColor(world_.machine(-r - 1).type);
            else if (world_.isLandfill({cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y})) px[i] = LANDFILL;
            else px[i] = explored ? oreMapColor(world_.tileType({cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y})) : BLACK;
            if (anyBridges && world_.bridgeIdAt({cx * CHUNK_SIZE + x, cy * CHUNK_SIZE + y}) >= 0) px[i] = bridgeColor;
        }
    if (it != mapChunks_.end()) {
        UpdateTexture(it->second.tex, px);   // same size: just new pixels
    } else {
        Image img{px, CHUNK_SIZE, CHUNK_SIZE, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D tex = LoadTextureFromImage(img);   // (copies the pixels)
        // clamp so the edge pixels never sample the opposite edge
        SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
        it = mapChunks_.emplace(key, MapChunkTex{tex, 0, false}).first;
    }
    it->second.version = version;
    it->second.explored = explored;
    return &it->second.tex;
}

bool hasSideArrows(MachineType t) { return machineHasOutputSides(t) || t == MachineType::Miner; }

void drawOutputSideArrows(MachineType t, Dir d, Rectangle dest, unsigned char alpha) {
    if (!hasSideArrows(t)) return;   // (a miner outputs on every side: all green)
    const Color outFill{90, 230, 110, alpha}, outEdge{20, 70, 30, alpha};
    const Color inFill{235, 80, 70, alpha}, inEdge{80, 20, 15, alpha};
    const float len = CELL_SIZE * 0.24f, wid = CELL_SIZE * 0.3f, inset = CELL_SIZE * 0.1f;
    float cx = dest.x + dest.width / 2, cy = dest.y + dest.height / 2;
    for (int k = 0; k < 4; ++k) {
        Dir side = Dir(k);
        bool out = isOutputSide(t, d, side);
        Vector2 v{float(dirVec(side).x), float(dirVec(side).y)}, n{-v.y, v.x};
        // just inside the machine at the middle tile of that side (the two
        // middle tiles if the side is an even number of tiles long);
        // green pointing out on output sides, red pointing in on input sides
        float ex0 = cx + v.x * (dest.width / 2 - inset), ey0 = cy + v.y * (dest.height / 2 - inset);
        float along = std::fabs(n.x) * dest.width + std::fabs(n.y) * dest.height;   // length of the side
        int tiles = std::max(1, int(std::lround(along / CELL_SIZE)));
        for (int i = (tiles - 1) / 2; i <= tiles / 2; ++i) {
            float o = (float(i) + 0.5f) * CELL_SIZE - along / 2;   // offset of this tile's middle
            float ex = ex0 + n.x * o, ey = ey0 + n.y * o;
            // tip on the edge (out) or a triangle-length further in (in)
            Vector2 tip = out ? Vector2{ex, ey} : Vector2{ex - v.x * len, ey - v.y * len};
            Vector2 base = out ? Vector2{ex - v.x * len, ey - v.y * len} : Vector2{ex, ey};
            Vector2 a{base.x + n.x * wid / 2, base.y + n.y * wid / 2}, b{base.x - n.x * wid / 2, base.y - n.y * wid / 2};
            DrawTriangle(tip, a, b, out ? outFill : inFill);   // (both windings: raylib draws only one)
            DrawTriangle(tip, b, a, out ? outFill : inFill);
            DrawTriangleLines(tip, a, b, out ? outEdge : inEdge);
        }
    }
}

void drawBlueprintMap(const Blueprint& bp, Rectangle sq) {
    fillRound(sq, 6, Color{GROUND.r, GROUND.g, GROUND.b, 255});
    if (bp.w <= 0 || bp.h <= 0) return;
    const float pad = 3;
    float tile = std::min((sq.width - 2 * pad) / float(bp.w), (sq.height - 2 * pad) / float(bp.h));
    float x0 = sq.x + (sq.width - tile * float(bp.w)) / 2, y0 = sq.y + (sq.height - tile * float(bp.h)) / 2;
    const Color beltColor{150, 150, 150, 255};
    for (Vec2i l : bp.landfill)
        DrawRectangleRec({x0 + float(l.x) * tile, y0 + float(l.y) * tile, tile, tile}, LANDFILL);
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

    // One tiny cached texture per chunk (terrain + belts + machines, see
    // mapChunkTexture); chunks the player hasn't walked to yet stay black
    // (and are NOT generated here). One black rectangle behind everything,
    // so zoomed far out it isn't thousands.
    DrawRectangleRec({float(cx0) * chunkPx, float(cy0) * chunkPx, float(cx1 - cx0 + 1) * chunkPx, float(cy1 - cy0 + 1) * chunkPx}, BLACK);
    // overlap neighbours by one screen pixel so no gap can appear between
    // chunks at fractional zoom levels
    const float pad = 1.0f / cam_.zoom;
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            if (const Texture2D* tex = mapChunkTexture(cx, cy))
                drawTexture(*tex, {cx * chunkPx, cy * chunkPx, chunkPx + pad, chunkPx + pad});

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
            if (const Texture2D* tex = mapChunkTexture(cx, cy))
                drawTexture(*tex, {mx(float(cx * chunkWorld)), my(float(cy * chunkWorld)), chunkPx + 0.5f, chunkPx + 0.5f});

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

void drawBridgeSprite(Rectangle r, Dir d, unsigned char alpha, bool start, bool end, const Color* overlay) {
    // drawn in "travel space": u along the bridge (0 = where items come in),
    // v across it, both 0..1, then turned to `d`
    auto at = [&](float u, float v) -> Vector2 {
        switch (d) {
            case Dir::Right: return {r.x + u * r.width, r.y + v * r.height};
            case Dir::Down: return {r.x + (1 - v) * r.width, r.y + u * r.height};
            case Dir::Left: return {r.x + (1 - u) * r.width, r.y + (1 - v) * r.height};
            default: return {r.x + v * r.width, r.y + (1 - u) * r.height};   // Up
        }
    };
    auto box = [&](float u0, float v0, float u1, float v1, Color c, float a) {
        Vector2 p = at(u0, v0), q = at(u1, v1);
        if (overlay) { c = *overlay; a = std::sqrt(a) * float(overlay->a) / 255.0f; }   // (faint parts get a faint tint)
        c.a = (unsigned char)(float(alpha) * a);
        DrawRectangleRec({std::min(p.x, q.x), std::min(p.y, q.y), std::fabs(q.x - p.x), std::fabs(q.y - p.y)}, c);
    };
    const Color steel{170, 175, 185, 255}, wood{120, 92, 62, 255}, rail{58, 46, 36, 255};
    box(0, 0.12f, 1, 0.88f, steel, 0.14f);                        // deck: you see through it
    for (float u = 0.0625f; u < 1; u += 0.25f) box(u, 0.12f, u + 0.06f, 0.88f, wood, 0.32f);   // planks
    box(0, 0.02f, 1, 0.13f, rail, 0.9f);                           // rails (the solid parts)
    box(0, 0.87f, 1, 0.98f, rail, 0.9f);
    for (float u : {0.0f, 0.5f}) {                                 // posts
        box(u, 0, u + 0.09f, 0.15f, rail, 1.0f);
        box(u, 0.85f, u + 0.09f, 1, rail, 1.0f);
    }
    if (start) box(0, 0.13f, 0.07f, 0.87f, rail, 0.7f);           // ramps at the ends
    if (end) box(0.93f, 0.13f, 1, 0.87f, rail, 0.7f);
    if (overlay) return;   // (no tint on the arrow)
    Color arrow{255, 255, 255, (unsigned char)(float(alpha) * 0.45f)};
    float th = std::max(1.5f, r.width * 0.06f);
    DrawLineEx(at(0.4f, 0.32f), at(0.6f, 0.5f), th, arrow);        // direction
    DrawLineEx(at(0.6f, 0.5f), at(0.4f, 0.68f), th, arrow);
}

void GameScreen::drawBelts() {
    world_.ensureTopology();
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    const Color tint{255, 255, 255, fadeGround() ? (unsigned char)77 : (unsigned char)255};   // top layer: ground faded
    for (int y = t; y < b; ++y)
        for (int x = l; x < r; ++x) {
            int id = world_.beltIdAt({x, y});
            if (id < 0) continue;
            const Belt& belt = world_.belt(id);
            Vector2 p = toScreen(float(x * CELL_SIZE), float(y * CELL_SIZE));
            drawTexture(assets::belt(belt.incomingMask, belt.dir), {p.x, p.y, CELL_SIZE, CELL_SIZE}, tint);
            if (belt.filter.enabled) drawFilterBadge(p.x + CELL_SIZE - 8, p.y + 8, 10);
        }
}

// The bridge layer, above belts (machines are drawn after it, but nothing
// stands under a bridge), with the items on it. Faded
// while deleting on the ground layer.
void GameScreen::drawBridges() {
    if (world_.bridges().empty()) return;
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    const unsigned char a = fadeBridges() ? 77 : 255;
    const float size = CELL_SIZE * 0.5f, T = float(BELT_TICKS_PER_TILE);
    auto same = [&](Vec2i p, Dir d) { int o = world_.bridgeIdAt(p); return o >= 0 && world_.belt(o).dir == d; };
    for (int y = t - 1; y < b + 1; ++y)
        for (int x = l - 1; x < r + 1; ++x) {
            int id = world_.bridgeIdAt({x, y});
            if (id < 0) continue;
            const Belt& br = world_.belt(id);
            Rectangle rc{float(x * CELL_SIZE), float(y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
            drawBridgeSprite(rc, br.dir, a, !same(br.pos - dirVec(br.dir), br.dir), !same(br.pos + dirVec(br.dir), br.dir));
            if (br.filter.enabled) drawFilterBadge(rc.x + CELL_SIZE - 8, rc.y + 8, 10);
        }
    for (int y = t - 1; y < b + 1; ++y)   // items on top (as on belts: sliding in from where they came)
        for (int x = l - 1; x < r + 1; ++x) {
            int id = world_.bridgeIdAt({x, y});
            if (id < 0 || world_.belt(id).item == NO_ITEM) continue;
            const Belt& br = world_.belt(id);
            float p = br.progress < BELT_TICKS_PER_TILE ? (float(br.progress) + interp_) : T;
            p = std::min(p, T) / T;
            Vec2i v = dirVec(br.itemFrom);
            float ex = float(x * CELL_SIZE + CELL_SIZE / 2), ey = float(y * CELL_SIZE + CELL_SIZE / 2);
            float sx = ex - float(v.x * CELL_SIZE), sy = ey - float(v.y * CELL_SIZE);
            drawItemIcon(br.item, {sx + (ex - sx) * p - size / 2, sy + (ey - sy) * p - size / 2, size, size}, a);
        }
}

void GameScreen::drawItems() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    const float size = CELL_SIZE * 0.5f;
    const float T = float(BELT_TICKS_PER_TILE);
    const unsigned char groundA = fadeGround() ? 77 : 255;
    auto drawOnTile = [&](ItemId item, Vec2i tile, int progress, Dir from) {
        float p = progress < BELT_TICKS_PER_TILE ? (float(progress) + interp_) : T;
        p = std::min(p, T) / T;
        Vec2i v = dirVec(from);
        float ex = float(tile.x * CELL_SIZE + CELL_SIZE / 2), ey = float(tile.y * CELL_SIZE + CELL_SIZE / 2);
        float sx = ex - float(v.x * CELL_SIZE), sy = ey - float(v.y * CELL_SIZE);
        Vector2 s = toScreen(sx + (ex - sx) * p, sy + (ey - sy) * p);
        drawItemIcon(item, {s.x - size / 2, s.y - size / 2, size, size}, groundA);
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
                    drawItemIcon(a.item, {s.x - size / 2, s.y - size / 2, size, size}, groundA);
                }
            }
        }
}

void GameScreen::drawMachines() {
    int l, t, r, b;
    cam_.visibleTiles(l, t, r, b);
    const bool buildingNow = mode_ == BuildMode::Building || bpMode_ == BpMode::Placing;
    const unsigned char groundA = fadeGround() ? 77 : 255;   // top layer: ground faded
    std::unordered_set<int> seen;
    for (int y = t - 3; y < b + 3; ++y)
        for (int x = l - 3; x < r + 3; ++x) {
            int id = world_.machineIdAt({x, y});
            if (id < 0 || !seen.insert(id).second) continue;
            const Machine& m = world_.machine(id);
            Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
            Rectangle dest{p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)};
            assets::drawMachine(m.type, m.dir, dest, Color{255, 255, 255, groundA});
            // output sides: while building, and on the machine under the mouse
            if (hasSideArrows(m.type) && (buildingNow || hovered_ == -(id + 1))) drawOutputSideArrows(m.type, m.dir, dest);
            float cx = dest.x + dest.width / 2, cy = dest.y + dest.height / 2;

            if (m.isProducing() && m.recipe && !m.recipe->outputs.empty()) {
                if (!showOutputs_) continue;   // Alt button off
                // icons of what it makes, centred (native 32 px like Python)
                const float s = 32, spacing = 4;
                float total = float(m.recipe->outputs.size()) * s + spacing * float(m.recipe->outputs.size() - 1);
                float xx = cx - std::floor(total / 2);
                for (auto& o : m.recipe->outputs) {
                    drawItemIcon(o.first, {xx, cy - s / 2, s, s}, groundA);
                    xx += s + spacing;
                }
            } else if (m.isStorage()) {
                if (!showOutputs_) continue;   // Alt mode off: no chest icons either
                ItemAmounts c = m.storage.contents();
                if (!c.empty()) {
                    auto best = std::max_element(c.begin(), c.end(), [](auto& a, auto& bb) { return a.second < bb.second; });
                    drawItemIcon(best->first, {cx - 16, cy - 16, 32, 32}, groundA);
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
        overlayBelt(world_.belt(hovered_ - 1), c);   // (a belt or a bridge: only on its pixels)
    } else {
        const Machine& m = world_.machine(-hovered_ - 1);
        Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
        overlayMachine(m.type, m.dir, {p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)}, c);
    }
}

// ---------------------------------------------------------------------------
// Build / delete previews
// ---------------------------------------------------------------------------
void GameScreen::drawGhostBelt(Vec2i tile, uint8_t mask, Dir out, Color overlay, bool hasOverlay) {
    Vector2 p = toScreen(float(tile.x * CELL_SIZE), float(tile.y * CELL_SIZE));
    Rectangle r{p.x, p.y, CELL_SIZE, CELL_SIZE};
    drawTexture(assets::belt(mask, out), r, Color{255, 255, 255, 160});
    if (hasOverlay) overlayBeltSprite(mask, out, r, overlay);
}

void GameScreen::drawBuildMode() {
    if (bpMode_ != BpMode::None) { drawBlueprintMode(); return; }
    if (mode_ == BuildMode::Building) {
        if (selected_ == Buildable::Belt) { drawOutputArrows(); drawGhostBelts(); }
        else if (selected_ == Buildable::Bridge) drawGhostBridge();
        else if (selected_ == Buildable::Landfill) drawGhostLandfill();
        else drawGhostMachine();
    } else if (mode_ == BuildMode::Deleting) {
        drawDeleteGhost();
    }
}

// Holding landfill: the square at the mouse - the water tiles it would fill
// in the landfill colour, land tiles just outlined; tinted like other ghosts
// when it can't be placed.
void GameScreen::drawGhostLandfill() {
    Vec2i tl = Builder::landfillTopLeft(mouseTile(), landfillSize_);
    Builder::LandfillCheck c = builder_.checkLandfill(tl, landfillSize_);
    std::unordered_set<Vec2i, Vec2iHash> fill(c.tiles.begin(), c.tiles.end());
    for (int dy = 0; dy < landfillSize_; ++dy)
        for (int dx = 0; dx < landfillSize_; ++dx) {
            Vec2i t{tl.x + dx, tl.y + dy};
            Rectangle r{float(t.x * CELL_SIZE), float(t.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
            if (fill.count(t)) DrawRectangleRec(r, Color{LANDFILL.r, LANDFILL.g, LANDFILL.b, 200});
        }
    Rectangle all{float(tl.x * CELL_SIZE), float(tl.y * CELL_SIZE), float(landfillSize_ * CELL_SIZE), float(landfillSize_ * CELL_SIZE)};
    Color tint{0, 0, 0, 0};
    switch (c.status) {
        case PlaceStatus::NoFunds: tint = {255, 255, 0, 70}; break;
        case PlaceStatus::NoWater: tint = {255, 0, 0, 60}; break;
        case PlaceStatus::Undiscovered: tint = {20, 20, 20, 170}; break;
        default: break;
    }
    if (tint.a) DrawRectangleRec(all, tint);
    DrawRectangleLinesEx(all, 2.0f / cam_.zoom, Color{255, 255, 255, 170});
}

// While holding a water pump: light up the free land tiles right next to
// water near the mouse - a pump covering one of them can be placed.
void GameScreen::drawPumpShore(Vec2i around) {
    const int R = 10;
    const Color fill{120, 220, 255, 70}, line{120, 220, 255, 170};
    for (int y = around.y - R; y <= around.y + R; ++y)
        for (int x = around.x - R; x <= around.x + R; ++x) {
            Vec2i p{x, y};
            if (world_.isWater(p) || world_.refAt(p) != 0) continue;
            if (!(world_.isWater({x + 1, y}) || world_.isWater({x - 1, y}) || world_.isWater({x, y + 1}) || world_.isWater({x, y - 1}))) continue;
            Rectangle rc{float(x * CELL_SIZE), float(y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
            DrawRectangleRec(rc, fill);
            DrawRectangleLinesEx(rc, 1, line);
        }
}

// ---------------------------------------------------------------------------
// Overlays only on a sprite's own pixels (see GameScreen.hpp)
// ---------------------------------------------------------------------------
namespace {
// the overlay colour (tint), with the sprite's alpha
#if defined(__EMSCRIPTEN__)
const char* OVERLAY_FS = R"(#version 100
precision mediump float;
varying vec2 fragTexCoord;
varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
void main() {
    float a = texture2D(texture0, fragTexCoord).a;
    gl_FragColor = vec4(fragColor.rgb * colDiffuse.rgb, fragColor.a * colDiffuse.a * a);
}
)";
#else
const char* OVERLAY_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
    float a = texture(texture0, fragTexCoord).a;
    finalColor = vec4(fragColor.rgb * colDiffuse.rgb, fragColor.a * colDiffuse.a * a);
}
)";
#endif
} // namespace

void GameScreen::overlayBegin() {
    if (!overlayLoaded_) {
        overlayLoaded_ = true;
        overlayShader_ = LoadShaderFromMemory(nullptr, OVERLAY_FS);
        overlayOk_ = overlayShader_.id != 0 && overlayShader_.id != rlGetShaderIdDefault();
    }
    if (overlayDepth_++ == 0 && overlayOk_) BeginShaderMode(overlayShader_);
}

void GameScreen::overlayEnd() {
    if (overlayDepth_ > 0 && --overlayDepth_ == 0 && overlayOk_) EndShaderMode();
}

void GameScreen::overlayBeltSprite(uint8_t mask, Dir d, Rectangle r, Color c) {
    overlayBegin();
    if (overlayOk_) drawTexture(assets::belt(mask, d), r, c);
    else DrawRectangleRec(r, c);   // (no shader: the whole tile)
    overlayEnd();
}

void GameScreen::overlayMachine(MachineType t, Dir d, Rectangle r, Color c) {
    overlayBegin();
    if (overlayOk_) assets::drawMachine(t, d, r, c);
    else DrawRectangleRec(r, c);
    overlayEnd();
}

void GameScreen::overlayBridge(Rectangle r, Dir d, Color c, bool start, bool end) {
    drawBridgeSprite(r, d, 255, start, end, &c);   // (plain shapes: no shader needed)
}

void GameScreen::overlayBelt(const Belt& b, Color c) {
    Rectangle r{float(b.pos.x * CELL_SIZE), float(b.pos.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
    if (!b.bridge) { overlayBeltSprite(b.incomingMask, b.dir, r, c); return; }
    auto same = [&](Vec2i p) { int o = world_.bridgeIdAt(p); return o >= 0 && world_.belt(o).dir == b.dir; };
    overlayBridge(r, b.dir, c, !same(b.pos - dirVec(b.dir)), !same(b.pos + dirVec(b.dir)));
}

// Hides `tiles` (bare terrain) only where ghost machine g's sprite has pixels,
// so the old thing still shows through the sprite's transparent parts:
//  1. the bare terrain of those tiles goes into a scratch texture,
//  2. the sprite is drawn into it with a blend that keeps each terrain pixel
//     only as much as the sprite covers it (colour and alpha * sprite alpha),
//  3. the result is laid over the world (premultiplied alpha).
void GameScreen::drawHiddenUnderSprite(const GhostMachine& g, const std::vector<Vec2i>& tiles) {
    constexpr int RT = 128;   // >= the biggest machine (3 x 3 tiles of CELL_SIZE)
    if (!maskRTReady_) {
        maskRT_ = LoadRenderTexture(RT, RT);
        maskRTReady_ = maskRT_.id != 0;
    }
    if (!maskRTReady_) {   // (no render textures: hide whole tiles)
        for (Vec2i t : tiles) drawTerrainTile(t);
        return;
    }
    Vec2i sz = machineSize(g.type, g.dir);
    Rectangle dest{float(g.tl.x * CELL_SIZE), float(g.tl.y * CELL_SIZE), float(sz.x * CELL_SIZE), float(sz.y * CELL_SIZE)};
    EndMode2D();
    BeginTextureMode(maskRT_);
    ClearBackground(BLANK);
    Camera2D c{};
    c.zoom = 1;
    c.target = {dest.x, dest.y};
    BeginMode2D(c);
    for (Vec2i t : tiles) drawTerrainTile(t);
    rlSetBlendFactors(RL_ZERO, RL_SRC_ALPHA, RL_FUNC_ADD);   // result = what is there * sprite alpha
    BeginBlendMode(BLEND_CUSTOM);
    assets::drawMachine(g.type, g.dir, dest, WHITE);
    EndBlendMode();
    EndMode2D();
    EndTextureMode();
    BeginMode2D(cam2d_);
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    // (render textures are upside down: the drawn area is at the bottom of the texture)
    DrawTexturePro(maskRT_.texture, {0, float(RT) - dest.height, dest.width, -dest.height}, dest, {0, 0}, 0, WHITE);
    EndBlendMode();
}

void GameScreen::drawReplaced(const std::vector<int>& belts, const std::vector<int>& machines, PlaceStatus st, const TileSet& ghost,
                              const std::vector<GhostMachine>& masked) {
    if (st != PlaceStatus::Ok && st != PlaceStatus::NoFunds && st != PlaceStatus::NoSpace) return;
    Color c = st == PlaceStatus::NoSpace ? Color{255, 165, 0, 150} : Color{255, 0, 0, 150};
    // 1) red on what goes - only on its own pixels, all of it (step 2 covers
    //    what is under the ghost, so red stays where it still shows through)
    overlayBegin();
    for (int id : belts) overlayBelt(world_.belt(id), c);
    for (int id : machines) {
        const Machine& m = world_.machine(id);
        RectF r = m.pixelRect();
        overlayMachine(m.type, m.dir, {r.x, r.y, r.w, r.h}, c);
    }
    overlayEnd();
    // 2) hide the covered tiles of what goes
    TileSet groundGone, bridgeGone;
    for (int id : belts)
        if (ghost.count(world_.belt(id).pos)) (world_.belt(id).bridge ? bridgeGone : groundGone).insert(world_.belt(id).pos);
    for (int id : machines) {
        const Machine& m = world_.machine(id);
        for (int dy = 0; dy < m.h; ++dy)
            for (int dx = 0; dx < m.w; ++dx)
                if (ghost.count(m.pos + Vec2i{dx, dy})) groundGone.insert(m.pos + Vec2i{dx, dy});
    }
    TileSet hidden = groundGone;
    hidden.insert(bridgeGone.begin(), bridgeGone.end());
    for (const GhostMachine& g : masked) {   // under ghost machines: pixel by pixel (nothing stays there)
        Vec2i sz = machineSize(g.type, g.dir);
        std::vector<Vec2i> under;
        for (int dy = 0; dy < sz.y; ++dy)
            for (int dx = 0; dx < sz.x; ++dx)
                if (hidden.erase(g.tl + Vec2i{dx, dy})) under.push_back(g.tl + Vec2i{dx, dy});
        if (!under.empty()) drawHiddenUnderSprite(g, under);
    }
    const unsigned char groundA = fadeGround() ? 77 : 255, bridgeA = fadeBridges() ? 77 : 255;
    auto same = [&](Vec2i p, Dir d) { int o = world_.bridgeIdAt(p); return o >= 0 && world_.belt(o).dir == d; };
    for (Vec2i t : hidden) {
        drawTerrainTile(t);
        if (!groundGone.count(t))   // only the bridge goes: the belt under it stays
            if (const Belt* g = world_.beltAt(t)) {
                Rectangle r{float(t.x * CELL_SIZE), float(t.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
                drawTexture(assets::belt(g->incomingMask, g->dir), r, Color{255, 255, 255, groundA});
                drawBeltItem(*g, groundA);
            }
        if (!bridgeGone.count(t))   // only the ground goes: the bridge over it stays
            if (int bid = world_.bridgeIdAt(t); bid >= 0) {
                const Belt& br = world_.belt(bid);
                Rectangle r{float(t.x * CELL_SIZE), float(t.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
                drawBridgeSprite(r, br.dir, bridgeA, !same(t - dirVec(br.dir), br.dir), !same(t + dirVec(br.dir), br.dir));
                drawBeltItem(br, bridgeA);
            }
    }
}

void GameScreen::drawGhostMachine() {
    MachineType type = toMachine(selected_);
    Dir dir = Dir(splitterRot_);   // splitters: 4 directions, a miner: standing / lying
    Vec2i size = machineSize(type, dir);
    Vec2i mt = mouseTile();
    Vec2i tl = Builder::machineTopLeft(type, mt, dir);
    PlacePlan pp;
    PlaceStatus st = builder_.checkMachine(type, mt, Dir(splitterRot_), shiftDown(), nullptr, nullptr, &pp);
    if (type == MachineType::WaterPump) drawPumpShore(mt);
    TileSet ghost;
    for (int dy = 0; dy < size.y; ++dy)
        for (int dx = 0; dx < size.x; ++dx) ghost.insert(tl + Vec2i{dx, dy});
    drawReplaced(pp.removeBelts, pp.removeMachines, st, ghost, {{type, dir, tl}});   // (Shift) what goes: hidden under the sprite, red outside

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
    drawOutputSideArrows(type, dir, dest, 200);
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
        case PlaceStatus::OnWater: case PlaceStatus::NeedsWater: overlay = {40, 110, 230, 110}; break;
        case PlaceStatus::Undiscovered: overlay = {20, 20, 20, 170}; break;
        default: break;
    }
    if (overlay.a) overlayMachine(type, dir, dest, overlay);
}

void GameScreen::drawGhostBelts() {
    world_.ensureTopology();
    Vec2i mt = mouseTile();
    bool allow = shiftDown();
    const Color red{255, 0, 0, 120}, orange{255, 165, 0, 120}, yellow{255, 255, 0, 120};
    const Color waterTint{40, 110, 230, 110};   // on water (same as machines)

    std::vector<BeltPlan> plan;
    std::vector<Color> colors;
    std::vector<bool> hasColor;
    if (!placingBelt_) {
        plan = builder_.planBelts(mt, mt, beltDir_);
        bool blocked = builder_.isDragTileBlocked(plan[0], mt, true, allow);
        if (builder_.beltTileOnWater(mt)) { colors.push_back(waterTint); hasColor.push_back(true); }
        else if (blocked) { colors.push_back(red); hasColor.push_back(true); }
        else {
            PlacePlan pp;   // a machine replaced / a belt turned in place: what goes (the bridge it fed)
            PlaceStatus st = builder_.beltAffordability(plan, nullptr, nullptr, &pp);
            colors.push_back(st == PlaceStatus::NoSpace ? orange : yellow);
            hasColor.push_back(st != PlaceStatus::Ok);
            drawReplaced(pp.removeBelts, pp.removeMachines, st, TileSet{mt});
        }
    } else {
        plan = builder_.planBelts(beltStart_, mt, beltDir_);
        if (builder_.anyDragTileBlocked(plan, beltStart_, allow)) {
            // water tiles blue; the rest red, unless water is the only problem
            bool othersOk = builder_.othersFreeOrMatching(plan, beltStart_), otherReason = false;
            for (auto& p : plan)
                otherReason |= !builder_.beltTileOnWater(p.pos) && builder_.isDragTileBlocked(p, beltStart_, othersOk, allow);
            for (auto& p : plan) {
                bool wet = builder_.beltTileOnWater(p.pos);
                colors.push_back(wet ? waterTint : red);
                hasColor.push_back(wet || otherReason);
            }
        } else {
            auto ok = builder_.runningAffordability(plan);
            for (bool o : ok) { colors.push_back(yellow); hasColor.push_back(!o); }
            PlacePlan pp;
            PlaceStatus st = builder_.beltAffordability(plan, nullptr, nullptr, &pp);
            TileSet ghost;
            for (auto& bp : plan) ghost.insert(bp.pos);
            drawReplaced(pp.removeBelts, pp.removeMachines, st, ghost);
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

// Holding a bridge: the straight line from the start (or the one tile at the
// mouse), tinted like belt ghosts.
void GameScreen::drawGhostBridge() {
    auto plan = bridgePlan();
    PlacePlan pp = builder_.checkBridge(plan, shiftDown());
    TileSet ghost;
    for (auto& bp : plan) ghost.insert(bp.pos);
    drawReplaced(pp.removeBelts, pp.removeMachines, pp.status, ghost);
    for (size_t i = 0; i < plan.size(); ++i) {
        Rectangle r{float(plan[i].pos.x * CELL_SIZE), float(plan[i].pos.y * CELL_SIZE), CELL_SIZE, CELL_SIZE};
        bool first = i == 0, last = i + 1 == plan.size();
        drawBridgeSprite(r, plan[i].dir, 190, first, last);
        PlaceStatus st = i < pp.each.size() ? pp.each[i] : PlaceStatus::Ok;
        Color tint{0, 0, 0, 0};
        if (st == PlaceStatus::Blocked || st == PlaceStatus::TooLong || st == PlaceStatus::Unconnected) tint = {255, 0, 0, 170};
        else if (st == PlaceStatus::Undiscovered) tint = {20, 20, 20, 200};
        else if (pp.status == PlaceStatus::NoFunds) tint = {255, 255, 0, 170};
        else if (pp.status == PlaceStatus::NoSpace) tint = {255, 165, 0, 170};
        if (tint.a) overlayBridge(r, plan[i].dir, tint, first, last);
    }
}

void GameScreen::drawDeleteGhost() {
    // bridges a deletion takes along (they would float): marked like the rest
    auto markTiles = [&](const std::vector<int>& ids, Color c) {
        overlayBegin();
        for (int b : ids) overlayBelt(world_.belt(b), c);
        overlayEnd();
    };
    if (deleteTargetsBridge(mouseTile())) {   // a bridge (Shift: all of it in a row) + the rest of it in front
        int id = world_.bridgeIdAt(mouseTile());
        std::vector<int> ids = shiftDown() ? builder_.bridgeRun(id) : std::vector<int>{id};
        Color c = builder_.canAffordDeletion(ids, {}) ? Color{255, 0, 0, 170} : Color{255, 165, 0, 170};
        markTiles(ids, c);
        markTiles(builder_.dependentBridges(ids, {}), c);
        return;
    }
    if (!hasGround(layer_) || dragDelete_ == DragDelete::Bridges) return;
    if (hoveredDelete_ == 0) {   // bare landfill: turned back into water
        Vec2i t = mouseTile();
        if (dragDelete_ == DragDelete::Objects || !builder_.canDeleteLandfillAt(t)) return;
        Color c = builder_.canAffordLandfillDeletion(1) ? Color{255, 0, 0, 100} : Color{255, 165, 0, 100};
        Vector2 p = toScreen(float(t.x * CELL_SIZE), float(t.y * CELL_SIZE));
        DrawRectangleRec({p.x, p.y, CELL_SIZE, CELL_SIZE}, c);
        return;
    }
    if (dragDelete_ == DragDelete::Landfill) return;   // this drag only deletes landfill
    bool shift = shiftDown();
    std::vector<int> belts;
    bool canDelete;
    if (hoveredDelete_ > 0) {
        belts = shift ? builder_.connectedBelts(hoveredDelete_ - 1) : std::vector<int>{hoveredDelete_ - 1};
        canDelete = builder_.canAffordDeletion(belts, {});
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
        canDelete = builder_.canAffordDeletion({}, {id});
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
    markTiles(hoveredDelete_ > 0 ? builder_.dependentBridges(belts, {}) : builder_.dependentBridges({}, {-hoveredDelete_ - 1}),
              Color{c.r, c.g, c.b, 170});
    if (hoveredDelete_ > 0) {
        overlayBegin();
        for (int id : belts) overlayBelt(world_.belt(id), c);
        overlayEnd();
    } else {
        const Machine& m = world_.machine(-hoveredDelete_ - 1);
        Vector2 p = toScreen(float(m.pos.x * CELL_SIZE), float(m.pos.y * CELL_SIZE));
        overlayMachine(m.type, m.dir, {p.x, p.y, float(m.w * CELL_SIZE), float(m.h * CELL_SIZE)}, c);
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
// points away, or runs past the side of a machine or chest - it pushes onto
// it) or blue "in" (the belt points into the machine and feeds it).
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
                if (r == 0) {   // free: what a belt here could do
                    bool canOut = m.isStorage() || isOutputSide(m.type, m.dir, ot.dir);
                    bool canIn = m.type != MachineType::Miner && !(machineHasOutputSides(m.type) && isOutputSide(m.type, m.dir, ot.dir));
                    if (canOut && canIn) {
                        arrow(cx + v.x * off, cy + v.y * off, v, both);
                        arrow(cx - v.x * off, cy - v.y * off, {-v.x, -v.y}, both);
                    } else if (canOut) {
                        arrow(cx, cy, v, both);
                    } else if (canIn) {
                        arrow(cx, cy, {-v.x, -v.y}, both);
                    }
                    continue;
                }
                const Belt& bt = world_.belt(r - 1);
                if (bt.incomingMask & dirBit(ot.dir)) arrow(cx, cy, v, out);   // pointing away or running past
                else if (bt.dir == opposite(ot.dir) && !(machineHasOutputSides(m.type) && isOutputSide(m.type, m.dir, ot.dir)))
                    arrow(cx, cy, {-v.x, -v.y}, in);   // (pointing into an output side: blocked, no arrow)
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

