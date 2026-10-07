// OpenFX plugin registry: discovery, the loaded-plugin table, and instance
// creation. Separate from OfxTypes.h so consumers that only need the instance
// model do not pull in plugin loading.
#pragma once

#include "ofx/OfxTypes.h"

#include <memory>
#include <string>
#include <vector>

struct PluginEntry {
  OfxPlugin *plugin;
  std::string label;
  std::string author;
  std::unique_ptr<Effect> descriptor;  // filter-context descriptor
  bool metalCapable = false;           // plugin declared kOfxImageEffectPropMetalRenderSupported
};
extern std::vector<PluginEntry> gPlugins;

// Scans the platform OFX directories, OFX_PLUGIN_PATH, and the app bundle.
void loadPlugins();
// Creates a plugin instance from a loaded plugin's descriptor.
std::unique_ptr<Effect> createInstance(PluginEntry &pe);
