#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include <fstream>
#include <iterator>
#include <string>

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
  g.uiFontScale = (int)gui.integer("uiFontScale", g.uiFontScale);
  // A project written before the scale setting stored a point size. Map it onto
  // the nearest of the three choices, so an existing preference still applies.
  if (!gui.find("uiFontScale") && gui.find("uiFontSizePt")) {
    const int oldPt = (int)gui.integer("uiFontSizePt", 13);
    if (oldPt <= 12) g.uiFontScale = 0;
    else if (oldPt <= 15) g.uiFontScale = 1;
    else g.uiFontScale = 2;
  }
  g.showPlugins = gui.boolean("showPlugins", g.showPlugins);
  g.showParameters = gui.boolean("showParameters", g.showParameters);
  // A project written before the panels were renamed stored showLeft/showRight,
  // so read the old key when the new one is absent.
  if (!gui.find("showPlugins")) g.showPlugins = gui.boolean("showLeft", g.showPlugins);
  if (!gui.find("showParameters")) g.showParameters = gui.boolean("showRight", g.showParameters);
  g.showFilmstrip = gui.boolean("showFilmstrip", g.showFilmstrip);
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

bool loadGlobalSettings(PersistGui &gui) {
  const std::string path = globalSettingsPath();
  if (path.empty()) return false;
  std::string json;
  if (!readAllText(path, json)) return false;
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (const JsonValue *guiValue = root.find("gui")) loadGui(*guiValue, gui);
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
