#include "services/Export.h"

#include "services/Workspace.h"
#include "engine/ExportJob.h"
#include "engine/ExportWorker.h"
#include "codecs/ImageIO.h"
#include "persist/ProjectPersist.h"
#include "services/ChainIO.h"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {

const char *exportExtension(ExportFormat fmt) {
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

// The single place that reads App for an export. Called after the render is idle,
// so the document and the chain cannot move underneath it.
ExportJob captureExportJob(Workspace &app, std::string outPath) {
  ExportJob job;
  job.src = app.document.full;
  job.outPath = std::move(outPath);
  job.sourcePath = app.document.path;
  job.previewW = app.document.preview.w;
  job.previewH = app.document.preview.h;
  // The chain writes the preview space; the export converts from it to the
  // chosen export space with the chosen rendering intent.
  job.chainSpace = app.render.outputSpace();
  job.exportSpace = app.gui.exportSpace;
  job.fileSpace = app.document.fileSpace;
  job.intent = app.gui.exportIntent;
  job.opts.bitDepth = app.gui.exportBitDepth;
  job.opts.quality = app.gui.exportQuality;
  job.opts.lossless = app.gui.exportLossless;
  job.chain = captureChain(app.chain);
  job.writeSidecar = app.gui.exportSidecar;
  return job;
}

}  // namespace

bool canExport(const Workspace &app) { return !app.document.full.px.empty() && !app.chain.nodes.empty(); }

std::string defaultExportName(const Workspace &app) {
  // Keep the source name so exports land beside it, but add a suffix so the
  // default never overwrites the original image.
  std::string stem = fs::path(app.document.path).stem().string();
  if (stem.empty()) stem = "export";
  else stem += "_export";
  return stem + exportExtension(app.gui.exportFormat);
}

void doExport(Workspace &app, const std::string &path) {
  if (!canExport(app)) return;
  std::string outPath = path;
  if (fs::path(outPath).extension().empty()) outPath += exportExtension(app.gui.exportFormat);

  // Wait for a quiet render before the job reads the document and the chain.
  app.render.waitIdle();
  ExportJob job = captureExportJob(app, std::move(outPath));
  app.reportStatus("Exporting full resolution...");
  const bool started = app.exporter.start(app.chain, std::move(job), app.render.schedule(),
                                          [&app](const ExportResult &result) { app.reportStatus(result.message); });
  if (!started) app.reportStatus("Export already in progress");
}
