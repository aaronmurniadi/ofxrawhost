#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include "core/Image.h"
#include "codecs/FileFormats.h"
#include "platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

JsonValue makeGuiJson(const PersistGui &g) {
  // Panel geometry lives in ImGui's layout .ini, not in this JSON.
  JsonValue gui = JsonValue::makeObject();
  gui.set("outputIndex", JsonValue::makeInt(g.outputIndex));
  gui.set("exportIndex", JsonValue::makeInt(g.exportIndex));
  gui.set("exportIntent", JsonValue::makeInt(g.exportIntent));
  gui.set("exportFormat", JsonValue::makeInt(g.exportFormat));
  gui.set("exportBitDepth", JsonValue::makeInt(g.exportBitDepth));
  gui.set("exportQuality", JsonValue::makeInt(g.exportQuality));
  gui.set("exportLossless", JsonValue::makeBool(g.exportLossless));
  gui.set("exportSidecar", JsonValue::makeBool(g.exportSidecar));
  gui.set("previewRes", JsonValue::makeInt(g.previewRes));
  gui.set("showPlugins", JsonValue::makeBool(g.showPlugins));
  gui.set("showParameters", JsonValue::makeBool(g.showParameters));
  gui.set("showFilmstrip", JsonValue::makeBool(g.showFilmstrip));
  return gui;
}

// The theme and the interface font size belong to the user, so a project, a
// sidecar, and a directory never record them; only the settings file does.
void writeAppearance(JsonValue &gui, const PersistAppearance &appearance) {
  gui.set("themeIndex", JsonValue::makeInt(appearance.themeIndex));
  gui.set("uiFontSizePt", JsonValue::makeInt(appearance.uiFontPt));
}

JsonValue makeUpdatesJson(const PersistUpdates &u) {
  // Only the settings file carries this object, so it stays out of makeGuiJson.
  JsonValue updates = JsonValue::makeObject();
  updates.set("channel", JsonValue::makeInt(u.channel));
  updates.set("checkAtStartup", JsonValue::makeBool(u.checkAtStartup));
  updates.set("devWarningShown", JsonValue::makeBool(u.devWarningShown));
  return updates;
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
    for (const auto &kv : n.params) params.set(kv.first, kv.second);
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

namespace {

// The state root of one file, with the absolute path of the file mirrored under
// it. The mirror keeps the name of the file readable, and two files that share a
// name in different directories cannot collide. Returns an empty path when the
// state directory cannot be determined, which sends the caller back beside the
// file.
fs::path stateRootFor(const std::string &path) {
  const fs::path state = userStateDir();
  if (state.empty()) return {};
  std::error_code ec;
  fs::path full = fs::weakly_canonical(fs::path(path), ec);
  if (ec) full = fs::absolute(fs::path(path), ec);
  if (ec) return {};
  // Dropping the root keeps the joined path relative, so the volume or the
  // leading slash is replaced by one "state" component instead of nesting.
  return state / full.relative_path();
}

// The state file of one file, named "<file name><suffix>.json".
std::string statePathFor(const std::string &path, const char *suffix) {
  const fs::path root = stateRootFor(path);
  if (root.empty()) return std::string();
  return (root.parent_path() / (root.filename().string() + suffix + ".json")).string();
}

}  // namespace

std::string workspaceProjectPath(const std::string &workspaceDir) {
  const fs::path root = stateRootFor(workspaceDir);
  // A directory is its own entry, so the state of a directory sits inside the
  // mirror of that directory rather than beside its other entries.
  if (root.empty()) return (fs::path(workspaceDir) / "workspace.ofxrawhost.json").string();
  return (root / "_directory.json").string();
}

std::string globalSettingsPath() {
  const fs::path dir = userConfigDir();
  if (dir.empty()) return std::string();
  return (dir / "settings.json").string();
}

std::string inputSidecarPath(const std::string &imagePath) {
  const std::string state = statePathFor(imagePath, "");
  if (!state.empty()) return state;
  return imagePath + ".ofxrawhost.json";
}

std::string exportSidecarPath(const std::string &exportPath) {
  const std::string state = statePathFor(exportPath, ".export");
  if (!state.empty()) return state;
  const fs::path p(exportPath);
  return (p.parent_path() / (p.stem().string() + ".json")).string();
}

std::string inputSidecarPathLegacy(const std::string &imagePath) { return imagePath + ".ofxrawhost.json"; }

std::string workspaceProjectPathLegacy(const std::string &workspaceDir) {
  return (fs::path(workspaceDir) / "workspace.ofxrawhost.json").string();
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

bool saveWorkspaceProject(const std::string &workspaceDir, const std::string &activeImageRel) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-workspace"));
  root.set("version", JsonValue::makeInt(1));
  root.set("activeImage", JsonValue::makeString(activeImageRel));
  return writeFile(workspaceProjectPath(workspaceDir), root.dump());
}

bool saveGlobalSettings(const PersistGui &gui, const PersistAppearance &appearance, const PersistUpdates &updates) {
  const std::string path = globalSettingsPath();
  if (path.empty()) return false;
  JsonValue guiJson = makeGuiJson(gui);
  writeAppearance(guiJson, appearance);
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-settings"));
  root.set("version", JsonValue::makeInt(1));
  root.set("gui", std::move(guiJson));
  root.set("updates", makeUpdatesJson(updates));
  return writeFile(path, root.dump());
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

bool saveInputSidecar(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
                      const PersistChain &chain) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-sidecar"));
  root.set("version", JsonValue::makeInt(1));
  root.set("kind", JsonValue::makeString("input"));
  root.set("sourcePath", JsonValue::makeString(fs::path(imagePath).filename().string()));
  root.set("inputColorSpace", JsonValue::makeString(colorSpaceName(fileSpace)));
  root.set("chainInputColorSpace", JsonValue::makeString(colorSpaceName(chainInputSpace)));
  root.set("chain", makeChainJson(chain));
  return writeFile(inputSidecarPath(imagePath), root.dump());
}

bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace fileSpace,
                       const PersistChain &chain) {
  JsonValue root = JsonValue::makeObject();
  root.set("format", JsonValue::makeString("ofxrawhost-sidecar"));
  root.set("version", JsonValue::makeInt(1));
  root.set("kind", JsonValue::makeString("export"));
  root.set("sourcePath", JsonValue::makeString(sourceImagePath));
  root.set("inputColorSpace", JsonValue::makeString(colorSpaceName(fileSpace)));
  root.set("exportedAt", JsonValue::makeString(iso8601Now()));
  root.set("chain", makeChainJson(chain));
  return writeFile(exportSidecarPath(exportPath), root.dump());
}
