#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include "imgio/ImageIO.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

JsonValue makeGuiJson(const PersistGui &g) {
  // Panel geometry lives in ImGui's layout .ini, not in this JSON.
  JsonValue gui = JsonValue::makeObject();
  gui.set("outputIndex", JsonValue::makeInt(g.outputIndex));
  gui.set("exportFormat", JsonValue::makeInt(g.exportFormat));
  gui.set("exportBitDepth", JsonValue::makeInt(g.exportBitDepth));
  gui.set("exportQuality", JsonValue::makeInt(g.exportQuality));
  gui.set("exportLossless", JsonValue::makeBool(g.exportLossless));
  gui.set("previewRes", JsonValue::makeInt(g.previewRes));
  gui.set("themeIndex", JsonValue::makeInt(g.themeIndex));
  gui.set("showLeft", JsonValue::makeBool(g.showLeft));
  gui.set("showRight", JsonValue::makeBool(g.showRight));
  gui.set("showFilmstrip", JsonValue::makeBool(g.showFilmstrip));
  return gui;
}

JsonValue makeChainJson(const PersistChain &chain) {
  JsonValue nodes = JsonValue::makeArray();
  for (const PersistNode &n : chain.nodes) {
    JsonValue node = JsonValue::makeObject();
    node.set("pluginIdentifier", JsonValue::makeString(n.pluginIdentifier));
    node.set("pluginLabel", JsonValue::makeString(n.pluginLabel));
    node.set("enabled", JsonValue::makeBool(n.enabled));
    JsonValue groups = JsonValue::makeObject();
    for (const auto &kv : n.groupOpen) groups.set(kv.first, JsonValue::makeBool(kv.second));
    node.set("groupOpen", std::move(groups));
    JsonValue params = JsonValue::makeObject();
    // Every value is already serialized JSON, so it is stored verbatim.
    for (const auto &kv : n.paramsJson) params.set(kv.first, JsonValue::fromRaw(kv.second));
    node.set("params", std::move(params));
    nodes.arr.push_back(std::move(node));
  }
  JsonValue chainValue = JsonValue::makeObject();
  chainValue.set("selectedNode", JsonValue::makeInt(chain.selectedNode));
  chainValue.set("nodes", std::move(nodes));
  return chainValue;
}

bool writeFile(const fs::path &path, const std::string &body) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f << body;
  return f.good();
}

std::string workspaceProjectPath(const std::string &workspaceDir) {
  return (fs::path(workspaceDir) / "workspace.ofxrawhost.json").string();
}

std::string inputSidecarPath(const std::string &imagePath) { return imagePath + ".ofxrawhost.json"; }

std::string exportSidecarPath(const std::string &exportPath) {
  fs::path p(exportPath);
  return (p.parent_path() / (p.stem().string() + ".json")).string();
}

bool isSupportedImagePath(const std::string &path) {
  if (isHostMetadataPath(path)) return false;
  return isSupportedImageExtension(lowerFileExtension(path));
}

bool isHostMetadataPath(const std::string &path) {
  static constexpr const char *kSuffix = ".ofxrawhost.json";
  return path.size() >= 18 && path.compare(path.size() - 18, 18, kSuffix) == 0;
}

std::vector<std::string> openImageDialogFilters() {
  std::string glob;
  for (const std::string &ext : supportedImageExtensions()) {
    if (!glob.empty()) glob += ' ';
    glob += '*';
    glob += ext;
  }
  return {"Images", glob};
}

std::vector<std::string> listWorkspaceImages(const std::string &workspaceDir) {
  std::vector<std::string> out;
  std::error_code ec;
  if (!fs::is_directory(workspaceDir, ec)) return out;
  for (const auto &ent : fs::recursive_directory_iterator(workspaceDir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) break;
    if (!ent.is_regular_file()) continue;
    const std::string p = ent.path().string();
    if (isHostMetadataPath(p)) continue;
    if (!isSupportedImagePath(p)) continue;
    out.push_back(p);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string relativeToWorkspace(const std::string &workspaceDir, const std::string &absPath) {
  std::error_code ec;
  fs::path base = fs::weakly_canonical(fs::path(workspaceDir), ec);
  fs::path file = fs::weakly_canonical(fs::path(absPath), ec);
  if (ec) return absPath;
  auto mm = std::mismatch(base.begin(), base.end(), file.begin(), file.end());
  if (mm.first == base.end()) {
    fs::path rel;
    for (auto it = mm.second; it != file.end(); ++it) rel /= *it;
    return rel.string();
  }
  return absPath;
}

bool saveWorkspaceProject(const std::string &workspaceDir, const PersistGui &gui, const std::string &activeImageRel) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-workspace"));
  root.set("version", JsonValue::makeInt(1));
  root.set("activeImage", JsonValue::makeString(activeImageRel));
  root.set("gui", makeGuiJson(gui));
  return writeFile(workspaceProjectPath(workspaceDir), root.dump());
}

static std::string iso8601Now() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

bool saveInputSidecar(const std::string &imagePath, ColorSpace inputSpace, const PersistGui &gui,
                      const PersistChain &chain) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-sidecar"));
  root.set("version", JsonValue::makeInt(1));
  root.set("kind", JsonValue::makeString("input"));
  root.set("sourcePath", JsonValue::makeString(fs::path(imagePath).filename().string()));
  root.set("inputColorSpace", JsonValue::makeString(colorSpaceName(inputSpace)));
  root.set("gui", makeGuiJson(gui));
  root.set("chain", makeChainJson(chain));
  return writeFile(inputSidecarPath(imagePath), root.dump());
}

bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace inputSpace,
                       const PersistGui &gui, const PersistChain &chain) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-sidecar"));
  root.set("version", JsonValue::makeInt(1));
  root.set("kind", JsonValue::makeString("export"));
  root.set("sourcePath", JsonValue::makeString(sourceImagePath));
  root.set("inputColorSpace", JsonValue::makeString(colorSpaceName(inputSpace)));
  root.set("exportedAt", JsonValue::makeString(iso8601Now()));
  root.set("gui", makeGuiJson(gui));
  root.set("chain", makeChainJson(chain));
  return writeFile(exportSidecarPath(exportPath), root.dump());
}
