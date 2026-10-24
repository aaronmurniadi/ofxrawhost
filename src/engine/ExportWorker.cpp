#include "engine/ExportWorker.h"

#include "engine/ChainRenderer.h"
#include "imgio/ImageIO.h"
#include "ofx/OfxRegistry.h"
#include "persist/ProjectPersist.h"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

ExportWorker::~ExportWorker() { wait(); }

void ExportWorker::wait() {
  if (thread_.joinable()) thread_.join();
}

bool ExportWorker::start(ChainState &chain, ExportJob job, RenderSchedule &schedule, Completion done) {
  if (busy_.exchange(true)) return false;
  // The previous export has finished (busy_ was false), so joining is safe.
  wait();

  thread_ = std::thread([this, &chain, &schedule, job = std::move(job), done = std::move(done)]() mutable {
    {
      // Hold the quiet window for the whole export: the preview worker stands
      // down and no chain mutation can start underneath the render.
      std::lock_guard<std::mutex> lock(schedule.mutex);
      schedule.exporting = true;
    }
    RenderSchedule::Guard busy(&schedule, true);

    // The preview was rendered at the preview size; the export runs at full size.
    for (auto &n : chain.nodes)
      if (n.instance) n.instance->setInputSize(job.src.w, job.src.h);

    ChainRenderer renderer;
    Image out;
    const OfxStatus status = renderer.render(chain, plugins(), job.src, out, 0);

    for (auto &n : chain.nodes)
      if (n.instance) n.instance->setInputSize(job.previewW, job.previewH);

    bool converted = true;
    if (status == kOfxStatOK && job.chainSpace != job.exportSpace)
      converted = convertColorSpace(out, job.chainSpace, job.exportSpace, job.intent);

    // A conversion failure leaves the pixels in the chain space, so tag them with
    // the space they are really in rather than mislabel the file.
    ColorSpace writeSpace = job.exportSpace;
    if (!converted) writeSpace = job.chainSpace;
    const bool ok = status == kOfxStatOK && writeImage(out, job.outPath, writeSpace, job.opts);
    if (ok && job.writeSidecar)
      saveExportSidecar(job.outPath, job.sourcePath, job.fileSpace, job.chain);

    ExportResult result;
    result.ok = ok;
    result.outPath = job.outPath;
    if (ok) {
      result.message = "Exported " + fs::path(job.outPath).filename().string() + " (" +
                       std::to_string(job.src.w) + "×" + std::to_string(job.src.h) + ")";
    } else {
      result.message = "Export failed (OFX status " + std::to_string(status) + ")";
    }
    busy_.store(false);
    if (done) done(result);
  });
  return true;
}
