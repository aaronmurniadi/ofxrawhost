#include "Actions.h"

#include "Filmstrip.h"
#include "NodeGraph.h"
#include "ChainRenderer.h"
#include "RenderScheduler.h"
#include "imgio/ImageIO.h"
#include "ofx/OfxHost.h"
#include "persist/DocumentActions.h"
#include "persist/ChainIO.h"
#include "persist/ProjectPersist.h"

#include <algorithm>
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
  app.doc.workspaceDir = fs::weakly_canonical(fs::path(dir), ec).string();
  refreshFilmstrip(app.filmstrip, app.doc.workspaceDir, app.doc.path);
  PersistGui wg;
  std::string activeRel;
  if (loadWorkspaceProject(app.doc.workspaceDir, wg, activeRel)) applyGui(app, wg);
  std::string toOpen;
  if (!activeRel.empty()) {
    fs::path p = fs::path(app.doc.workspaceDir) / activeRel;
    if (fs::is_regular_file(p, ec)) toOpen = p.string();
  }
  if (toOpen.empty() && !app.filmstrip.entries.empty()) toOpen = app.filmstrip.entries[0].path;
  if (!toOpen.empty())
    openPath(app, toOpen);
  else
    app.setStatus("Workspace: " + fs::path(app.doc.workspaceDir).filename().string() + " (no images)");
  persistWorkspace(app);
  app.gui.showFilmstrip = true;
  return true;
}

void openPath(App &app, const std::string &path) {
  if (isHostMetadataPath(path)) {
    app.setStatus("Sidecar files (.ofxrawhost.json) are not images — open the image file instead.");
    return;
  }
  if (!app.doc.path.empty() && app.doc.path != path) saveCurrentInputSidecar(app);
  Image img;
  ColorSpace detected = ColorSpace::LinearRec2020;
  if (!loadImage(path, img, detected)) {
    app.setStatus("Could not decode " + fs::path(path).filename().string());
    return;
  }
  waitRenderIdle(app);  // worker reads inputSpace; stop it before swapping the image
  app.doc.path = path;
  app.doc.full = std::move(img);
  app.doc.inputSpace = detected;
  app.gui.previewZoom = 1.0f;
  app.gui.previewPanX = 0.0f;
  app.gui.previewPanY = 0.0f;
  app.setStatus("Loaded " + fs::path(path).filename().string() + " (" + colorSpaceName(detected) + ")");
  app.filmstrip.index = filmstripIndexForPath(app.filmstrip, path);
  PersistSidecar sc;
  if (loadSidecarFile(inputSidecarPath(path), sc)) {
    applyGui(app, sc.gui);
    applyChain(app, sc.chain);
  }
  rebuildPreview(app);
  persistWorkspace(app);
}

static const char *exportExtension(ExportFormat fmt) {
  switch (fmt) {
    case ExportFormat::PNG:
      return ".png";
    case ExportFormat::TIFF:
      return ".tif";
    case ExportFormat::WEBP:
      return ".webp";
    case ExportFormat::JXL:
      return ".jxl";
    default:
      return ".jpg";
  }
}

bool canExport(const App &app) { return !app.doc.full.px.empty() && !app.chain.nodes.empty(); }

std::string defaultExportName(const App &app) {
  // Keep the source name so exports land beside it, but add a suffix so the
  // default never overwrites the original image.
  std::string stem = fs::path(app.doc.path).stem().string();
  if (stem.empty()) stem = "export";
  else stem += "_export";
  return stem + exportExtension(app.gui.exportFormat);
}

void setOutputTag(App &app, int index) {
  app.outputTag = outputSpace(index);
  scheduleDisplayRecolor(app);
}

void setPreviewRes(App &app, int index) {
  app.gui.previewRes = static_cast<PreviewRes>(std::clamp(index, 0, kPreviewResCount - 1));
  rebuildPreview(app);
}

void doExport(App &app, const std::string &path) {
  if (!canExport(app)) return;
  std::string outPath = path;
  if (fs::path(outPath).extension().empty()) outPath += exportExtension(app.gui.exportFormat);
  if (app.render.exportInFlight.exchange(true)) {
    app.setStatus("Export already in progress");
    return;
  }
  // The previous export has finished (exportInFlight was false), so joining is safe.
  if (app.render.exportThread.joinable()) app.render.exportThread.join();

  app.setStatus("Exporting full resolution...");
  waitRenderIdle(app);
  {
    std::lock_guard<std::mutex> lock(app.render.schedule.mutex);
    app.render.schedule.exporting = true;
  }
  const int pw = app.doc.preview.w, ph = app.doc.preview.h;
  Image src = app.doc.full;
  const ColorSpace space = app.outputTag;
  const ColorSpace inSpace = app.doc.inputSpace;
  EncodeOptions opts;
  opts.bitDepth = app.gui.exportBitDepth;
  opts.quality = app.gui.exportQuality;
  opts.lossless = app.gui.exportLossless;
  const PersistGui persistGui = captureGui(app);
  const PersistChain persistChain = captureChain(app);
  const std::string sourcePath = app.doc.path;
  app.render.exportThread =
      std::thread([&, src, outPath, pw, ph, space, opts, persistGui, persistChain, sourcePath, inSpace]() mutable {
        RenderSchedule::Guard busy(&app.render.schedule, true);
        for (auto &n : app.chain.nodes)
          if (n.instance) n.instance->setInputSize(src.w, src.h);
        ChainRenderer renderer;
        Image out;
        OfxStatus st = renderer.render(app.chain, gPlugins, src, out, 0);
        for (auto &n : app.chain.nodes)
          if (n.instance) n.instance->setInputSize(pw, ph);
        bool ok = st == kOfxStatOK && writeImage(out, outPath, space, opts);
        if (ok) saveExportSidecar(outPath, sourcePath, inSpace, persistGui, persistChain);
        app.setStatus(ok ? "Exported " + fs::path(outPath).filename().string() + " (" + std::to_string(src.w) + "×" +
                                std::to_string(src.h) + ")"
                         : "Export failed (OFX status " + std::to_string(st) + ")");
        app.render.exportInFlight = false;
      });
}
