// sim/Save.hpp - JSON save files, compatible with the Python version.
// (The C++ version writes a few extra fields, like item progress on belts;
// the Python version simply ignores them, so saves work in both directions.)
#pragma once
#include "World.hpp"
#include "Player.hpp"
#include "Blueprint.hpp"
#include <string>
#include <vector>

// A named pin on the map (P key), saved with the game. `symbol` is an
// optional picture in front of the name: "item:<id>", "machine:<type>",
// "belt" or "" (none).
struct MapMarker { Vec2i tile; std::string name; std::string symbol; };

// View settings saved with the game: camera, the Icons (Alt) toggle and the
// map markers.
struct CameraState {
    float x = 0, y = 0; bool valid = false; float zoom = 1.0f; bool showOutputs = true;
    std::vector<MapMarker> markers;
    double playSeconds = 0;   // time played in this game (shown top right)
};

namespace save {

// Desktop: nothing to do. Browser: saves live in the browser (IndexedDB);
// initStorage() starts loading them, storageReady() says when that's done.
// Every write is copied back to the browser storage automatically.
void initStorage();
bool storageReady();

void setSavesDir(const std::string& dir);   // default "saves"
const std::string& savesDir();

bool isValidName(const std::string& name);
std::string sanitizeName(const std::string& name); // "" if invalid
bool exists(const std::string& name);
std::vector<std::string> listSaves();

// Time played in a save (seconds), -1 if it can't be read. Reads the file.
double readPlayTime(const std::string& name);
void remove(const std::string& name);
bool rename(const std::string& oldName, const std::string& newName);
std::string lastOpened();                 // "" if none
void setLastOpened(const std::string& name);

bool saveGame(const World& w, const Player& p, const CameraState& cam, const std::string& name, std::string* error = nullptr);
// The same, but only copying the world happens now (a few ms); building and
// writing the file runs on a background thread (desktop; the browser saves
// right away). Everything else in save:: that reads or writes save files
// waits for it first. takeSaveError() reports a background save that failed.
void saveGameInBackground(const World& w, const Player& p, const CameraState& cam, const std::string& name);
void waitForSaves();
bool takeSaveError(std::string& error);
bool loadGame(World& w, Player& p, CameraState& cam, const std::string& name, std::string* error = nullptr);
// Loading removes everything that stands on water (saves from before lakes
// and seas) - with its contents, no refund. Returns how many things.
int removeBuiltOnWater(World& w);
// Fresh world + empty inventory; also writes the save immediately.
void newGame(World& w, Player& p, CameraState& cam, const std::string& name);

// Blueprints are shared by all save games (saves/blueprints.bpjson).
// Blueprint books are stored in the same file (`books` may be null when
// they aren't needed). A blueprint whose book doesn't exist is put outside.
std::vector<Blueprint> loadBlueprints(std::vector<BlueprintBook>* books = nullptr);
bool saveBlueprints(const std::vector<Blueprint>& list, const std::vector<BlueprintBook>& books = {});

} // namespace save
