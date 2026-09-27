#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"

#include "imgio/ImageIO.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::string jsonEscape(const std::string &s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\b': o += "\\b"; break;
      case '\f': o += "\\f"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default: o += (char)c;
    }
  }
  return o;
}

void appendGuiJson(std::ostringstream &o, const PersistGui &g) {
  // Panel sizes (leftW/rightW/filmstripH) are legacy; layout lives in ImGui .ini.
  o << "\"gui\":{"
    << "\"outputIndex\":" << g.outputIndex << ","
    << "\"exportFormat\":" << g.exportFormat << ","
    << "\"jpegQuality\":" << g.jpegQuality << ","
    << "\"previewRes\":" << g.previewRes << ","
    << "\"themeIndex\":" << g.themeIndex << ","
    << "\"showLeft\":" << (g.showLeft ? "true" : "false") << ","
    << "\"showRight\":" << (g.showRight ? "true" : "false") << ","
    << "\"showFilmstrip\":" << (g.showFilmstrip ? "true" : "false") << "}";
}

void appendChainJson(std::ostringstream &o, const PersistChain &chain) {
  o << "\"chain\":{"
    << "\"selectedNode\":" << chain.selectedNode << ","
    << "\"nodes\":[";
  for (size_t i = 0; i < chain.nodes.size(); ++i) {
    const PersistNode &n = chain.nodes[i];
    if (i) o << ',';
    o << '{'
      << "\"pluginIdentifier\":\"" << jsonEscape(n.pluginIdentifier) << "\","
      << "\"pluginLabel\":\"" << jsonEscape(n.pluginLabel) << "\","
      << "\"enabled\":" << (n.enabled ? "true" : "false") << ","
      << "\"groupOpen\":{";
    size_t gi = 0;
    for (const auto &kv : n.groupOpen) {
      if (gi++) o << ',';
      o << '"' << jsonEscape(kv.first) << "\":" << (kv.second ? "true" : "false");
    }
    o << "},\"params\":{";
    size_t pi = 0;
    for (const auto &kv : n.paramsJson) {
      if (pi++) o << ',';
      o << '"' << jsonEscape(kv.first) << "\":" << kv.second;
    }
    o << "}}";
  }
  o << "]}";
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
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);
  if (e == ".exr" || e == ".tif" || e == ".tiff" || e == ".png" || e == ".jpg" || e == ".jpeg") return true;
  if (e == ".cr2" || e == ".cr3" || e == ".nef" || e == ".arw" || e == ".dng" || e == ".raf" || e == ".orf" ||
      e == ".rw2" || e == ".pef" || e == ".srw" || e == ".raw")
    return true;
  return false;
}

bool isHostMetadataPath(const std::string &path) {
  static constexpr const char *kSuffix = ".ofxrawhost.json";
  return path.size() >= 18 && path.compare(path.size() - 18, 18, kSuffix) == 0;
}

std::vector<std::string> openImageDialogFilters() {
  return {"Images",
          "*.exr *.tif *.tiff *.png *.jpg *.jpeg *.cr2 *.cr3 *.nef *.arw *.dng *.raf *.orf *.rw2 *.pef *.srw *.raw"};
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
  std::ostringstream o;
  o << '{'
    << "\"format\":\"ofxrawhost-workspace\","
    << "\"version\":1,"
    << "\"activeImage\":\"" << jsonEscape(activeImageRel) << "\",";
  appendGuiJson(o, gui);
  o << '}';
  return writeFile(workspaceProjectPath(workspaceDir), o.str());
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
  std::ostringstream o;
  o << '{'
    << "\"format\":\"ofxrawhost-sidecar\","
    << "\"version\":1,"
    << "\"kind\":\"input\","
    << "\"sourcePath\":\"" << jsonEscape(fs::path(imagePath).filename().string()) << "\","
    << "\"inputColorSpace\":\"" << jsonEscape(colorSpaceName(inputSpace)) << "\",";
  appendGuiJson(o, gui);
  o << ',';
  appendChainJson(o, chain);
  o << '}';
  return writeFile(inputSidecarPath(imagePath), o.str());
}

bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace inputSpace,
                       const PersistGui &gui, const PersistChain &chain) {
  std::ostringstream o;
  o << '{'
    << "\"format\":\"ofxrawhost-sidecar\","
    << "\"version\":1,"
    << "\"kind\":\"export\","
    << "\"sourcePath\":\"" << jsonEscape(sourceImagePath) << "\","
    << "\"inputColorSpace\":\"" << jsonEscape(colorSpaceName(inputSpace)) << "\","
    << "\"exportedAt\":\"" << iso8601Now() << "\",";
  appendGuiJson(o, gui);
  o << ',';
  appendChainJson(o, chain);
  o << '}';
  return writeFile(exportSidecarPath(exportPath), o.str());
}

