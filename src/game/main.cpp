// game/main.cpp - window, main loop, menu <-> game switching.
// The same code runs as the Windows game and in the browser (Emscripten):
// one call of frame() = one frame; the desktop loops it, the browser calls it
// from requestAnimationFrame.
#include "raylib.h"
#include "Assets.hpp"
#include "MainMenu.hpp"
#include "GameScreen.hpp"
#include "../sim/Save.hpp"
#include <memory>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {
struct App {
    std::unique_ptr<MainMenu> menu;   // created once the save folder is ready
    std::unique_ptr<GameScreen> game;
    bool quit = false;
};

#ifdef __EMSCRIPTEN__
// Browser: keep the canvas as big as the page.
EM_JS(int, pageWidth, (), { return window.innerWidth; });
EM_JS(int, pageHeight, (), { return window.innerHeight; });
void fitToPage() {
    int w = pageWidth(), h = pageHeight();
    if (w > 0 && h > 0 && (w != GetScreenWidth() || h != GetScreenHeight())) SetWindowSize(w, h);
}
#endif

void frame(App& app) {
#ifdef __EMSCRIPTEN__
    fitToPage();
    if (!save::storageReady()) {   // saves are still being loaded from the browser
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Loading...", 20, 20, 20, RAYWHITE);
        EndDrawing();
        return;
    }
#endif
    if (!app.menu) app.menu = std::make_unique<MainMenu>();
    MainMenu& menu = *app.menu;
    float dt = GetFrameTime();
    std::vector<Event> events = pollEvents();

    if (!app.game) {
        for (const Event& e : events) {
            MenuAction a = menu.handle(e);
            if (a.kind == MenuAction::Quit) app.quit = true;
            else if (a.kind == MenuAction::NewGame) {
                app.game = std::make_unique<GameScreen>();
                app.game->newGame(a.name);
                break;
            } else if (a.kind == MenuAction::LoadGame) {
                auto g = std::make_unique<GameScreen>();
                std::string err;
                if (g->loadGame(a.name, &err)) { app.game = std::move(g); break; }
                menu.refresh();
                menu.showMessage("Could not load '" + a.name + "': " + err);
            }
        }
        BeginDrawing();
        if (app.game) ClearBackground(BLACK);
        else menu.draw(dt);
        EndDrawing();
        return;
    }

    for (const Event& e : events) app.game->handle(e);
    if (app.game->returnToMenuRequested()) {
        app.game.reset();
        menu.backToStart();
        // Finish this frame properly: raylib only reads new input in
        // EndDrawing(). Skipping it made the main menu see the same click
        // ("Exit" -> "Yes"/"No") again and press "New Game" under it.
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
        return;
    }
    app.game->update(dt);
    BeginDrawing();
    app.game->draw();
    EndDrawing();
}

#ifdef __EMSCRIPTEN__
void webFrame(void* app) { frame(*static_cast<App*>(app)); }
#endif
} // namespace

int main() {
#ifndef __EMSCRIPTEN__
    // Run from the folder the .exe lives in so assets/ and saves/ are found
    // no matter how the game was started.
    ChangeDirectory(GetApplicationDirectory());
    if (!DirectoryExists("assets") && DirectoryExists("../assets")) ChangeDirectory("..");
#endif
    save::initStorage();   // browser: load the saves kept in the browser

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "Satis Factorio Program");
#ifndef __EMSCRIPTEN__
    SetWindowMinSize(1100, 700);
#endif
    SetExitKey(KEY_NULL); // ESC is used by the game itself
    SetTargetFPS(0);

    assets::load();

    static App app;
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(webFrame, &app, 0, 1);   // never returns
#else
    while (!WindowShouldClose() && !app.quit) frame(app);
    app.game.reset();
    app.menu.reset();
    assets::unload();
    CloseWindow();
#endif
    return 0;
}
