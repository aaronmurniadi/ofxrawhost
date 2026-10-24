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

// The loaded plugins. Owned by the registry instead of living in a mutable
// process global, so the table has one owner and one lifetime.
class OfxRegistry {
 public:
  std::vector<PluginEntry> &entries() { return entries_; }
  const std::vector<PluginEntry> &entries() const { return entries_; }
  bool empty() const { return entries_.empty(); }
  int count() const { return (int)entries_.size(); }
  // Index of the plugin whose label matches, or -1.
  int indexOfLabel(const std::string &label) const;

 private:
  std::vector<PluginEntry> entries_;
};

// The one registry of the process, for the same reason the OFX host handle is
// one: plugins are discovered once and every Effect belongs to that host.
OfxRegistry &ofxRegistry();
// Shorthand for the table every caller actually wants.
inline std::vector<PluginEntry> &plugins() { return ofxRegistry().entries(); }

// Scans the platform OFX directories, OFX_PLUGIN_PATH, and the app bundle.
void loadPlugins();
// Creates a plugin instance from a loaded plugin's descriptor.
std::unique_ptr<Effect> createInstance(PluginEntry &pe);
