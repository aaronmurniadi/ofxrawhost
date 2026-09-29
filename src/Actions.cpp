#include "Actions.h"

#include "Filmstrip.h"
#include "NodeGraph.h"
#include "RenderPipeline.h"
#include "imgio/ImageIO.h"
#include "persist/DocumentActions.h"
#include "persist/ProjectPersist.h"

#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

bool openWorkspace(App &app, const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    app.setStatus("Not a directory");
    return false;
  }
  saveCurrentInputSidecar(app);
  persistWorkspace(app);
  app.workspaceDir = fs::weakly_canonical(fs::path(dir), ec).string();
  refreshFilmstrip(app);
  PersistGui wg;
  std::string activeRel;
  if (loadWorkspaceProject(app.workspaceDir, wg, activeRel)) applyGui(app, wg);
  std::string toOpen;
  if (!activeRel.empty()) {
    fs::path p = fs::path(app.workspaceDir) / activeRel;
    if (fs::is_regular_file(p, ec)) toOpen = p.string();
  }
  if (toOpen.empty() && !app.filmstrip.empty()) toOpen = app.filmstrip[0].path;
  if (!toOpen.empty())
    openPath(app, toOpen);
  else
    app.setStatus("Workspace: " + fs::path(app.workspaceDir).filename().string() + " (no images)");
  persistWorkspace(app);
  return true;
}

void openPath(App &app, const std::string &path) {
  if (isHostMetadataPath(path)) {
    app.setStatus("Sidecar files (.ofxrawhost.json) are not images — open the image file instead.");
    return;
  }
  if (!app.path.empty() && app.path != path) saveCurrentInputSidecar(app);
  Image img;
  ColorSpace detected = ColorSpace::LinearRec2020;
  if (!loadImage(path, img, detected)) {
    app.setStatus("Could not decode " + fs::path(path).filename().string());
    return;
  }
  app.path = path;
  app.full = std::move(img);
  app.inputSpace = detected;
  app.previewZoom = 1.0f;
  app.previewPanX = 0.0f;
  app.previewPanY = 0.0f;
  app.setStatus("Loaded " + fs::path(path).filename().string() + " (" + colorSpaceName(detected) + ")");
  app.filmstripIndex = filmstripIndexForPath(app, path);
  PersistSidecar sc;
  if (loadSidecarFile(inputSidecarPath(path), sc)) {
    applyGui(app, sc.gui);
    applyChain(app, sc.chain);
  }
  rebuildPreview(app);
  persistWorkspace(app);
}

static const char *exportExtension(ExportFormat fmt) {
  return fmt == ExportFormat::PNG ? ".png" : ".jpg";
}

bool canExport(const App &app) { return !app.full.px.empty() && !app.nodes.empty(); }

std::string defaultExportName(const App &app) {
  return fs::path(app.path).stem().string() + exportExtension(app.exportFormat);
}

void doExport(App &app, const std::string &path) {
  if (!canExport(app)) return;
  std::string outPath = path;
  if (fs::path(outPath).extension().empty()) outPath += exportExtension(app.exportFormat);

  app.setStatus("Exporting full resolution...");
  waitRenderIdle(app);
  const int pw = app.preview.w, ph = app.preview.h;
  Image src = app.full;
  const ColorSpace space = app.outputTag;
  const ColorSpace inSpace = app.inputSpace;
  const int jpegQuality = app.jpegQuality;
  const PersistGui persistGui = captureGui(app);
  const PersistChain persistChain = captureChain(app);
  const std::string sourcePath = app.path;
  std::thread([&, src, outPath, pw, ph, space, jpegQuality, persistGui, persistChain, sourcePath, inSpace]() mutable {
    for (auto &n : app.nodes)
      if (n.instance) {
        n.instance->w = src.w;
        n.instance->h = src.h;
      }
    Image out;
    OfxStatus st = renderChain(app, src, out, 0);
    for (auto &n : app.nodes)
      if (n.instance) {
        n.instance->w = pw;
        n.instance->h = ph;
      }
    bool ok = st == kOfxStatOK && writeImage(out, outPath, space, jpegQuality);
    if (ok) saveExportSidecar(outPath, sourcePath, inSpace, persistGui, persistChain);
    app.setStatus(ok ? "Exported " + fs::path(outPath).filename().string() + " (" + std::to_string(src.w) + "×" +
                            std::to_string(src.h) + ")"
                      : "Export failed (OFX status " + std::to_string(st) + ")");
  }).detach();
}
