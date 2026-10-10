// spektrafilm .spkpreset import: decode the obfuscated snapshot the plugin
// writes, then push its values into a live plugin instance. The preset JSON
// layout and the payload obfuscation are owned by the spektrafilm OFX plugin,
// so this module is the only place that knows them.
#pragma once

#include <string>
#include <vector>

struct Effect;
struct Param;

// True for every identifier in the spektrafilm family (org.spektrafilm and its
// flavor variants such as org.spektrafilm.flow).
bool isSpektrafilmPlugin(const char *pluginIdentifier);

// Decodes the .spkpreset file at path and writes each matching parameter into
// effect. Returns the parameters it changed. On failure it returns an empty
// list and fills error.
std::vector<Param *> applySpektrafilmPreset(const std::string &path, Effect *effect, std::string &error);
