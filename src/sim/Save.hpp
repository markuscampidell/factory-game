// sim/Save.hpp - JSON save files, compatible with the Python version.
// (The C++ version writes a few extra fields, like item progress on belts;
// the Python version simply ignores them, so saves work in both directions.)
#pragma once
#include "World.hpp"
#include "Player.hpp"
#include "Blueprint.hpp"
#include <string>
#include <vector>

struct CameraState { float x = 0, y = 0; bool valid = false; float zoom = 1.0f; bool showOutputs = true; };

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
void remove(const std::string& name);
bool rename(const std::string& oldName, const std::string& newName);
std::string lastOpened();                 // "" if none
void setLastOpened(const std::string& name);

bool saveGame(const World& w, const Player& p, const CameraState& cam, const std::string& name, std::string* error = nullptr);
bool loadGame(World& w, Player& p, CameraState& cam, const std::string& name, std::string* error = nullptr);
// Fresh world + starting inventory; also writes the save immediately.
void newGame(World& w, Player& p, CameraState& cam, const std::string& name);

// Blueprints are shared by all save games (saves/blueprints.bpjson).
std::vector<Blueprint> loadBlueprints();
bool saveBlueprints(const std::vector<Blueprint>& list);

} // namespace save
