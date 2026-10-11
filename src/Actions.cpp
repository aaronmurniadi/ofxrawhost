#include "Actions.h"

#include "Filmstrip.h"
#include "NodeGraph.h"
#include "ChainRenderer.h"
#include "RenderScheduler.h"
#include "imgio/ImageIO.h"
#include "ofx/OfxHost.h"
#include "ParamBridge.h"
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
    app.setStatus("Directory: " + fs::path(app.doc.workspaceDir).filename().string() + " (no images)");
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
  ColorSpace working = ColorSpace::LinearRec709;
  if (!loadImage(path, img, detected, working)) {
    app.setStatus("Could not decode " + fs::path(path).filename().string());
    return;
  }
  waitRenderIdle(app);  // worker reads inputSpace; stop it before swapping the image
  app.doc.path = path;
  app.doc.full = std::move(img);
  app.doc.fileSpace = detected;
  app.doc.workingSpace = working;
  app.doc.inputSpace = working;
  app.gui.previewZoom = 1.0f;
  app.gui.previewPanX = 0.0f;
  app.gui.previewPanY = 0.0f;
  app.setStatus("Loaded " + fs::path(path).filename().string() + " (" + colorSpaceName(detected) + ")");
  app.filmstrip.index = filmstripIndexForPath(app.filmstrip, path);
  PersistSidecar sc;
  if (loadSidecarFile(inputSidecarPath(path), sc)) {
    // A sidecar that recorded a chain input space restores that choice.
    app.doc.inputSpace = colorSpaceFromName(sc.chainInputColorSpace, app.doc.workingSpace);
    applyGui(app, sc.gui);
    applyChain(app, sc.chain);
  }
  rebuildPreview(app);
  // The chain carries the document input space into its first plugin, and no
  // sidecar may have run, so re-derive it for the image that is now open.
  syncChainColorSpaces(app);
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

void setInputSpace(App &app, int index) {
  app.doc.inputSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  // The chain carries this space into its first plugin, and a later plugin can
  // change what leaves the chain, so the whole walk and a render are needed.
  syncChainColorSpaces(app);
  scheduleRender(app);
}

void setPreviewSpace(App &app, int index) {
  // The preview is the destination of the last conversion: the chain output is
  // encoded into this space and shown as it is. Only the display conversion
  // changes, so a re-render is not needed.
  app.gui.previewSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  scheduleDisplayRecolor(app);
}

void setExportSpace(App &app, int index) {
  app.gui.exportSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
}

void setExportIntent(App &app, int intentIndex) {
  app.gui.exportIntent = static_cast<RenderIntent>(std::clamp(intentIndex, 0, kRenderIntentCount - 1));
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
  // The chain writes the preview space; the export converts from it to the
  // chosen export space with the chosen rendering intent.
  const ColorSpace space = app.chainOutputSpace;
  const ColorSpace exportSpace = app.gui.exportSpace;
  const RenderIntent intent = app.gui.exportIntent;
  const ColorSpace inSpace = app.doc.fileSpace;
  EncodeOptions opts;
  opts.bitDepth = app.gui.exportBitDepth;
  opts.quality = app.gui.exportQuality;
  opts.lossless = app.gui.exportLossless;
  const PersistGui persistGui = captureGui(app);
  const PersistChain persistChain = captureChain(app);
  const std::string sourcePath = app.doc.path;
  app.render.exportThread =
      std::thread([&, src, outPath, pw, ph, space, exportSpace, intent, opts, persistGui, persistChain, sourcePath,
                   inSpace]() mutable {
        RenderSchedule::Guard busy(&app.render.schedule, true);
        for (auto &n : app.chain.nodes)
          if (n.instance) n.instance->setInputSize(src.w, src.h);
        ChainRenderer renderer;
        Image out;
        OfxStatus st = renderer.render(app.chain, gPlugins, src, out, 0);
        for (auto &n : app.chain.nodes)
          if (n.instance) n.instance->setInputSize(pw, ph);
        bool converted = true;
        if (st == kOfxStatOK && space != exportSpace) converted = convertColorSpace(out, space, exportSpace, intent);
        // A conversion failure leaves the pixels in the preview space, so tag
        // them with the space they are really in rather than mislabel the file.
        ColorSpace writeSpace = exportSpace;
        if (!converted) writeSpace = space;
        bool ok = st == kOfxStatOK && writeImage(out, outPath, writeSpace, opts);
        if (ok) saveExportSidecar(outPath, sourcePath, inSpace, persistGui, persistChain);
        app.setStatus(ok ? "Exported " + fs::path(outPath).filename().string() + " (" + std::to_string(src.w) + "×" +
                                std::to_string(src.h) + ")"
                         : "Export failed (OFX status " + std::to_string(st) + ")");
        app.render.exportInFlight = false;
      });
}
