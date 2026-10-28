#include "services/managers/export/ExportManager.h"

#include "engine/ExportJob.h"
#include "engine/ExportWorker.h"
#include "model/GuiState.h"
#include "persist/daos/SidecarDao.h"
#include "services/ChainIO.h"
#include "services/Workspace.h"

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

// The single place that reads workspace for an export. Called after the render is
// idle, so the document and the chain cannot move underneath it.
ExportJob captureExportJob(Workspace &workspace, std::string outPath) {
  ExportJob job;
  job.src = workspace.document.full;
  job.outPath = std::move(outPath);
  job.sourcePath = workspace.document.path;
  job.previewW = workspace.document.preview.w;
  job.previewH = workspace.document.preview.h;
  // The chain writes the preview space; the export converts from it to the
  // chosen export space with the chosen rendering intent.
  job.chainSpace = workspace.render.outputSpace();
  job.exportSpace = workspace.gui.exportSpace;
  job.fileSpace = workspace.document.fileSpace;
  job.intent = workspace.gui.exportIntent;
  job.opts.bitDepth = workspace.gui.exportBitDepth;
  job.opts.quality = workspace.gui.exportQuality;
  job.opts.lossless = workspace.gui.exportLossless;
  job.chain = captureChain(workspace.chain);
  job.writeSidecar = workspace.gui.exportSidecar;
  return job;
}

class ExportManagerImpl : public ExportManager {
 public:
  ExportManagerImpl(Workspace &workspace, SidecarDao &sidecar) : workspace_(workspace), sidecar_(sidecar) {}

  bool canExport() const override {
    return !workspace_.document.full.px.empty() && !workspace_.chain.nodes.empty();
  }

  std::string defaultExportName() const override {
    // Keep the source name so exports land beside it, but add a suffix so the
    // default never overwrites the original image.
    std::string stem = fs::path(workspace_.document.path).stem().string();
    if (stem.empty())
      stem = "export";
    else
      stem += "_export";
    return stem + exportExtension(workspace_.gui.exportFormat);
  }

  void doExport(const std::string &path) override {
    if (!canExport()) return;
    std::string outPath = path;
    if (fs::path(outPath).extension().empty()) outPath += exportExtension(workspace_.gui.exportFormat);

    // Wait for a quiet render before the job reads the document and the chain.
    workspace_.render.waitIdle();
    ExportJob job = captureExportJob(workspace_, std::move(outPath));
    // SidecarDao is injected for the later move of export-sidecar writes out of
    // ExportWorker. The worker still owns that write today.
    (void)sidecar_;
    workspace_.reportStatus("Exporting full resolution...");
    const bool started =
        workspace_.exporter.start(workspace_.chain, std::move(job), workspace_.render.schedule(),
                                  [this](const ExportResult &result) { workspace_.reportStatus(result.message); });
    if (!started) workspace_.reportStatus("Export already in progress");
  }

 private:
  Workspace &workspace_;
  SidecarDao &sidecar_;
};

}  // namespace

std::unique_ptr<ExportManager> createExportManager(Workspace &workspace, SidecarDao &sidecar) {
  return std::make_unique<ExportManagerImpl>(workspace, sidecar);
}
