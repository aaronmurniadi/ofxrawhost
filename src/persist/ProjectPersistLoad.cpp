#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include "platform/SystemUiFont.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace {

// A settings file written while the setting was a scale of the system size
// carries "uiFontScale" rather than a point size: 0 was smaller, 1 the system
// size, and 2 bigger. The conversion applies the same multipliers to the system
// size, so the choice survives the upgrade.
int legacyFontScalePt(int scale) {
  if (scale <= 0) return (int)std::lround(systemUiFontSizePt() * 0.85f);
  if (scale >= 2) return (int)std::lround(systemUiFontSizePt() * 1.2f);
  return 0;
}

}  // namespace

void loadGui(const JsonValue &gui, PersistGui &g) {
  g.outputIndex = (int)gui.integer("outputIndex", g.outputIndex);
  g.exportIndex = (int)gui.integer("exportIndex", g.exportIndex);
  g.exportIntent = (int)gui.integer("exportIntent", g.exportIntent);
  g.exportFormat = (int)gui.integer("exportFormat", g.exportFormat);
  g.exportBitDepth = (int)gui.integer("exportBitDepth", g.exportBitDepth);
  g.exportQuality = (int)gui.integer("exportQuality", g.exportQuality);
  // Older sidecars stored the lossy quality under "jpegQuality".
  if (!gui.find("exportQuality")) g.exportQuality = (int)gui.integer("jpegQuality", g.exportQuality);
  g.exportLossless = gui.boolean("exportLossless", g.exportLossless);
  g.previewRes = (int)gui.integer("previewRes", g.previewRes);
  g.themeIndex = (int)gui.integer("themeIndex", g.themeIndex);
  if (gui.find("uiFontSizePt")) g.uiFontPt = (int)gui.integer("uiFontSizePt", g.uiFontPt);
  else if (gui.find("uiFontScale")) g.uiFontPt = legacyFontScalePt((int)gui.integer("uiFontScale", 1));
  g.showPlugins = gui.boolean("showPlugins", g.showPlugins);
  g.showParameters = gui.boolean("showParameters", g.showParameters);
  // A project written before the panels were renamed stored showLeft/showRight,
  // so read the old key when the new one is absent.
  if (!gui.find("showPlugins")) g.showPlugins = gui.boolean("showLeft", g.showPlugins);
  if (!gui.find("showParameters")) g.showParameters = gui.boolean("showRight", g.showParameters);
  g.showFilmstrip = gui.boolean("showFilmstrip", g.showFilmstrip);
}

void loadUpdates(const JsonValue &updates, PersistUpdates &u) {
  u.channel = (int)updates.integer("channel", u.channel);
  u.checkAtStartup = updates.boolean("checkAtStartup", u.checkAtStartup);
  u.devWarningShown = updates.boolean("devWarningShown", u.devWarningShown);
}

bool loadChain(const JsonValue &chain, PersistChain &out) {
  out.selectedNode = (int)chain.integer("selectedNode", out.selectedNode);
  const JsonValue *nodes = chain.find("nodes");
  if (!nodes || nodes->kind != JsonValue::Kind::Array) return false;
  out.nodes.clear();
  for (const JsonValue &node : nodes->arr) {
    if (node.kind != JsonValue::Kind::Object) return false;
    PersistNode n;
    n.pluginIdentifier = node.text("pluginIdentifier", n.pluginIdentifier);
    n.pluginLabel = node.text("pluginLabel", n.pluginLabel);
    n.enabled = node.boolean("enabled", n.enabled);
    if (const JsonValue *groups = node.find("groupOpen"))
      for (const auto &kv : groups->obj) n.groupOpen[kv.first] = kv.second.kind == JsonValue::Kind::Bool && kv.second.b;
    if (const JsonValue *params = node.find("params"))
      for (const auto &kv : params->obj) n.paramsJson[kv.first] = kv.second.dump();
    out.nodes.push_back(std::move(n));
  }
  return true;
}

bool readAllText(const std::string &path, std::string &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return true;
}

bool loadWorkspaceProject(const std::string &workspaceDir, PersistGui &gui, std::string &activeImageRel) {
  std::string json;
  if (!readAllText(workspaceProjectPath(workspaceDir), json)) return false;
  JsonValue root;
  if (JsonValue::parse(json, root)) {
    if (const JsonValue *guiValue = root.find("gui")) loadGui(*guiValue, gui);
    activeImageRel = root.text("activeImage", activeImageRel);
  }
  return true;
}

bool loadGlobalSettings(PersistGui &gui, PersistUpdates &updates) {
  const std::string path = globalSettingsPath();
  if (path.empty()) return false;
  std::string json;
  if (!readAllText(path, json)) return false;
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (const JsonValue *guiValue = root.find("gui")) loadGui(*guiValue, gui);
  if (const JsonValue *updatesValue = root.find("updates")) loadUpdates(*updatesValue, updates);
  return true;
}

bool loadSidecarFile(const std::string &path, PersistSidecar &out) {
  std::string json;
  if (!readAllText(path, json)) return false;
  JsonValue root;
  if (JsonValue::parse(json, root)) {
    out.kind = root.text("kind", out.kind);
    out.sourcePath = root.text("sourcePath", out.sourcePath);
    out.inputColorSpace = root.text("inputColorSpace", out.inputColorSpace);
    out.chainInputColorSpace = root.text("chainInputColorSpace", out.chainInputColorSpace);
    out.exportedAt = root.text("exportedAt", out.exportedAt);
    if (const JsonValue *guiValue = root.find("gui")) loadGui(*guiValue, out.gui);
    if (const JsonValue *chainValue = root.find("chain")) loadChain(*chainValue, out.chain);
  }
  return true;
}
