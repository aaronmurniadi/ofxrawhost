#include "services/managers/export/ExportManager.h"

#include "engine/ExportJob.h"
#include "engine/ExportWorker.h"
#include "model/GuiState.h"
#include "persist/daos/SidecarDao.h"
#include "services/ChainIO.h"
#include "services/ManagerSession.h"

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

// The single place that reads session state for an export. Called after the
// render is idle, so the document and the chain cannot move underneath it.
ExportJob captureExportJob(ManagerSession &session, std::string outPath) {
  ExportJob job;
  job.src = session.document.full;
  job.outPath = std::move(outPath);
  job.sourcePath = session.document.path;
  job.previewW = session.document.preview.w;
  job.previewH = session.document.preview.h;
  // The chain writes the preview space; the export converts from it to the
  // chosen export space with the chosen rendering intent.
  job.chainSpace = session.render.outputSpace();
  job.exportSpace = session.gui.exportSpace;
  job.fileSpace = session.document.fileSpace;
  job.intent = session.gui.exportIntent;
  job.opts.bitDepth = session.gui.exportBitDepth;
  job.opts.quality = session.gui.exportQuality;
  job.opts.lossless = session.gui.exportLossless;
  job.chain = captureChain(session.chain);
  job.writeSidecar = session.gui.exportSidecar;
  return job;
}

class ExportManagerImpl : public ExportManager {
 public:
  ExportManagerImpl(ManagerSession session, SidecarDao &sidecar) : session_(session), sidecar_(sidecar) {}

  bool canExport() const override {
    return !session_.document.full.px.empty() && !session_.chain.nodes.empty();
  }

  std::string defaultExportName() const override {
    // Keep the source name so exports land beside it, but add a suffix so the
    // default never overwrites the original image.
    std::string stem = fs::path(session_.document.path).stem().string();
    if (stem.empty())
      stem = "export";
    else
      stem += "_export";
    return stem + exportExtension(session_.gui.exportFormat);
  }

  void doExport(const ExportRequest &request) override {
    // Validation is business logic, so it lives here and not in the dialog.
    if (!canExport()) return;
    if (request.outPath.empty()) return;
    std::string outPath = request.outPath;
    if (fs::path(outPath).extension().empty()) outPath += exportExtension(session_.gui.exportFormat);

    // Wait for a quiet render before the job reads the document and the chain.
    session_.render.waitIdle();
    ExportJob job = captureExportJob(session_, std::move(outPath));
    // SidecarDao is injected for the later move of export-sidecar writes out of
    // ExportWorker. The worker still owns that write today.
    (void)sidecar_;
    session_.reportStatus("Exporting full resolution...");
    const bool started =
        session_.exporter.start(session_.chain, std::move(job), session_.render.schedule(),
                                [this](const ExportResult &result) { session_.reportStatus(result.message); });
    if (!started) session_.reportStatus("Export already in progress");
  }

 private:
  ManagerSession session_;
  SidecarDao &sidecar_;
};

}  // namespace

std::unique_ptr<ExportManager> createExportManager(ManagerSession session, SidecarDao &sidecar) {
  return std::make_unique<ExportManagerImpl>(session, sidecar);
}
