// Host-side file work for the bundled spektrafilm plugin.
//
// The plugin exposes "Load Preset" and "Export LUT" push buttons but carries no
// dialog code: it declares the buttons and expects the host to run them. The
// file work (decode a .spkpreset, place the .cube the plugin wrote) lives here so
// the widgets stay drawing code and this logic is reachable from the self-test.
//
// Asking for a path is the interface's job: every entry point below takes a path.

#pragma once

#include "ofx/OfxTypes.h"

#include <string>

struct Node;

// True when this push button is one the host must run instead of notifying the
// plugin. name is "loadPreset" or "exportLut".
bool isHostRunButton(const Node &node, const Param *p, const char *name);

struct PresetImportResult {
  int changed = 0;        // parameters the preset moved
  std::string message;    // ready for the status bar
};

// Decodes path and applies it to node, notifying the plugin for each parameter it
// moved. A failed decode leaves the instance untouched.
PresetImportResult importSpektrafilmPreset(Node &node, const std::string &path);

struct LutExportResult {
  std::string produced;   // the file the plugin reports, empty when it said nothing
  bool reported = false;  // the plugin raised a message at all
};

// Runs the plugin's Export LUT handler and returns the file it reports. The
// plugin writes the .cube itself, so its message is the only place the host
// learns where the file landed.
LutExportResult runSpektrafilmLutExport(Node &node, Param *button);

// Moves the file the plugin wrote to target. A rename fails across volumes, so
// this copies first and deletes only on success.
std::string placeLutExport(const std::string &produced, const std::string &target);

// File filters the interfaces offer for the two buttons.
const char *presetDialogFilter();
const char *lutDialogFilter();
