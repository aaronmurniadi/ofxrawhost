#pragma once

#include "core/Json.h"
#include "persist/ProjectPersist.h"

#include <filesystem>
#include <string>

// Builders for the "gui", "updates", and the "chain" object of a workspace file,
// the settings file, or a sidecar.
JsonValue makeGuiJson(const PersistGui &g);
// The theme and the interface font size are app-level, so only the settings file
// writes and reads them. The keys sit in the "gui" object, which is where a
// settings file written before this split already kept them.
void writeAppearance(JsonValue &gui, const PersistAppearance &appearance);
void loadAppearance(const JsonValue &gui, PersistAppearance &appearance);
JsonValue makeUpdatesJson(const PersistUpdates &u);
JsonValue makeChainJson(const PersistChain &chain);

bool writeFile(const std::filesystem::path &path, const std::string &body);
bool readAllText(const std::string &path, std::string &out);

// Readers take an already parsed object, so the file layout lives in one place.
void loadGui(const JsonValue &gui, PersistGui &g);
void loadUpdates(const JsonValue &updates, PersistUpdates &u);
bool loadChain(const JsonValue &chain, PersistChain &out);
