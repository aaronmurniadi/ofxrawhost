#pragma once

#include "persist/Json.h"
#include "persist/ProjectPersist.h"

#include <filesystem>
#include <string>

// Builders for the "gui", "updates", and the "chain" object of a workspace file,
// the settings file, or a sidecar.
JsonValue makeGuiJson(const PersistGui &g);
JsonValue makeUpdatesJson(const PersistUpdates &u);
JsonValue makeChainJson(const PersistChain &chain);

bool writeFile(const std::filesystem::path &path, const std::string &body);
bool readAllText(const std::string &path, std::string &out);

// Readers take an already parsed object, so the file layout lives in one place.
void loadGui(const JsonValue &gui, PersistGui &g);
void loadUpdates(const JsonValue &updates, PersistUpdates &u);
bool loadChain(const JsonValue &chain, PersistChain &out);
