#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include <fstream>
#include <iterator>
#include <string>

void loadGui(const JsonValue &gui, PersistGui &g) {
  g.outputIndex = (int)gui.integer("outputIndex", g.outputIndex);
  g.exportFormat = (int)gui.integer("exportFormat", g.exportFormat);
  g.jpegQuality = (int)gui.integer("jpegQuality", g.jpegQuality);
  g.previewRes = (int)gui.integer("previewRes", g.previewRes);
  g.themeIndex = (int)gui.integer("themeIndex", g.themeIndex);
  g.showLeft = gui.boolean("showLeft", g.showLeft);
  g.showRight = gui.boolean("showRight", g.showRight);
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

bool loadSidecarFile(const std::string &path, PersistSidecar &out) {
  std::string json;
  if (!readAllText(path, json)) return false;
  JsonValue root;
  if (JsonValue::parse(json, root)) {
    out.kind = root.text("kind", out.kind);
    out.sourcePath = root.text("sourcePath", out.sourcePath);
    out.inputColorSpace = root.text("inputColorSpace", out.inputColorSpace);
    out.exportedAt = root.text("exportedAt", out.exportedAt);
    if (const JsonValue *guiValue = root.find("gui")) loadGui(*guiValue, out.gui);
    if (const JsonValue *chainValue = root.find("chain")) loadChain(*chainValue, out.chain);
  }
  return true;
}
