#include "engine/RenderWorker.h"

#include "core/RenderContext.h"
#include "core/perf.h"
#include "engine/ChainRenderer.h"
#include "ofx/OfxRegistry.h"

#include <algorithm>
#include <string>
#include <utility>

// Long-edge cap for the fast interactive draft pass. The full pass runs at the
// preview size, so a draft is only built when the preview is larger than this.
static constexpr int kDraftMaxEdge = 1280;

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

RenderWorker::~RenderWorker() { stop(); }

void RenderWorker::start() {
  if (thread_.joinable()) return;
  thread_ = std::thread([this] { run(); });
}

void RenderWorker::stop() {
  if (!thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(schedule_.mutex);
    schedule_.pending = false;
    schedule_.recolorPending = false;
  }
  schedule_.cv.notify_all();
  thread_.join();
}

void RenderWorker::waitIdle() {
  renderContext().cancelInFlight();
  schedule_.waitIdle();
}

void RenderWorker::publish(Image img, ColorSpace from, ColorSpace to) {
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(img, from, to, rgba);
  std::lock_guard<std::mutex> lock(displayMutex_);
  display_ = std::move(img);
  displayRGBA_ = std::move(rgba);
  displayDirty_ = true;
}

void RenderWorker::showSourcePreview() {
  const Image &preview = host_->previewImage();
  if (preview.px.empty()) return;
  publish(preview, host_->workingSpace(), host_->previewSpace());
}

void RenderWorker::request() {
  if (host_->quitting()) return;
  if (host_->chain().nodes.empty() || host_->previewImage().px.empty()) {
    showSourcePreview();
    return;
  }
  renderContext().cancelInFlight();
  {
    std::lock_guard<std::mutex> lock(schedule_.mutex);
    schedule_.pending = true;
  }
  schedule_.cv.notify_one();
}

void RenderWorker::recolor() {
  if (host_->quitting()) return;
  renderContext().cancelInFlight();
  {
    std::lock_guard<std::mutex> lock(schedule_.mutex);
    schedule_.recolorPending = true;
    schedule_.pending = true;
  }
  schedule_.cv.notify_one();
}

void RenderWorker::rebuildPreview() {
  if (host_->fullImage().px.empty()) return;
  // makePreview rewrites the preview image, which the worker may be reading.
  waitIdle();
  const int maxEdge = host_->wantedPreviewMaxEdge();
  builtPreviewMaxEdge_ = maxEdge;
  makePreview(host_->fullImage(), maxEdge, host_->mutablePreviewImage());
  request();
}

void RenderWorker::pumpUpload(GlTexture &texture) {
  PerfScope _ps("uploadTextureRGBA");
  std::lock_guard<std::mutex> lock(displayMutex_);
  if (!displayDirty_ || displayRGBA_.empty() || display_.w <= 0 || display_.h <= 0) return;
  texture.upload(displayRGBA_.data(), display_.w, display_.h);
  displayDirty_ = false;
}

Image RenderWorker::displayCopy() const {
  std::lock_guard<std::mutex> lock(displayMutex_);
  return display_;
}

// Proxy scale of a pass: how much smaller than the project the frame being
// rendered is. Plugins that size something in project pixels (the denoiser's
// noise model, for one) read it from the render arguments, so a preview tells
// them the same story an export does.
static double passRenderScale(int projectEdge, const Image &rendered) {
  const int edge = std::max(rendered.w, rendered.h);
  if (projectEdge <= 0 || edge <= 0) return 1.0;
  if (edge >= projectEdge) return 1.0;
  return (double)edge / (double)projectEdge;
}

void RenderWorker::run() {
  ChainRenderer renderer;
  while (!host_->quitting()) {
    bool recolorOnly = false;
    {
      std::unique_lock<std::mutex> lock(schedule_.mutex);
      schedule_.cv.wait(lock, [this] {
        return host_->quitting() || (schedule_.pending.load() && !schedule_.exporting);
      });
      if (host_->quitting()) break;
      recolorOnly = schedule_.recolorPending;
      schedule_.recolorPending = false;
      schedule_.pending = false;
      schedule_.busy = true;
    }
    RenderSchedule::Guard busy(&schedule_);
    if (recolorOnly) {
      Image img;
      ColorSpace from;
      ColorSpace to;
      {
        std::lock_guard<std::mutex> lock(displayMutex_);
        if (display_.px.empty()) continue;
        img = display_;
      }
      if (host_->chain().nodes.empty()) from = host_->workingSpace();
      else from = host_->outputSpace();
      to = host_->previewSpace();
      publish(std::move(img), from, to);
      continue;
    }
    if (host_->chain().nodes.empty() || host_->previewImage().px.empty()) continue;
    const int gen = renderContext().beginPass();
    host_->reportStatus("Rendering...");
    const Image &preview = host_->previewImage();
    Image out;
    const int longEdge = std::max(preview.w, preview.h);
    if (longEdge > kDraftMaxEdge) {
      Image draftInput;
      if (makePreview(preview, kDraftMaxEdge, draftInput)) {
        const OfxStatus draftSt = renderer.render(host_->chain(), plugins(), draftInput, out, gen, true,
                                                  passRenderScale(host_->projectLongEdge(), draftInput));
        if (!renderContext().isCurrent(gen)) continue;
        if (draftSt == kOfxStatOK) publish(std::move(out), host_->outputSpace(), host_->previewSpace());
      }
    }
    if (!renderContext().isCurrent(gen)) continue;
    const OfxStatus st = renderer.render(host_->chain(), plugins(), preview, out, gen, false,
                                         passRenderScale(host_->projectLongEdge(), preview));
    if (!renderContext().isCurrent(gen)) continue;
    if (st == kOfxStatOK) {
      const int ow = out.w, oh = out.h;
      publish(std::move(out), host_->outputSpace(), host_->previewSpace());
      host_->reportStatus(std::to_string(ow) + "×" + std::to_string(oh) + " preview");
    } else {
      host_->reportStatus("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}
