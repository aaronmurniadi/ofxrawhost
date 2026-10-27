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

RenderWorker::~RenderWorker() { stop(); }

void RenderWorker::start() {
  if (thread_.joinable()) return;
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void RenderWorker::stop() {
  if (!thread_.joinable()) return;
  stopping_ = true;
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

void RenderWorker::showSourcePreview(const RenderInputs &inputs) {
  if (!inputs.hasPreview()) return;
  publish(*inputs.preview, inputs.workingSpace, inputs.previewSpace);
}

void RenderWorker::request(const RenderInputs &inputs) {
  if (stopping_) return;
  if (!inputs.hasChain() || !inputs.hasPreview()) {
    showSourcePreview(inputs);
    return;
  }
  renderContext().cancelInFlight();
  {
    std::lock_guard<std::mutex> lock(schedule_.mutex);
    inputs_ = inputs;
    schedule_.pending = true;
  }
  schedule_.cv.notify_one();
}

void RenderWorker::recolor(const RenderInputs &inputs) {
  if (stopping_) return;
  renderContext().cancelInFlight();
  {
    std::lock_guard<std::mutex> lock(schedule_.mutex);
    inputs_ = inputs;
    schedule_.recolorPending = true;
    schedule_.pending = true;
  }
  schedule_.cv.notify_one();
}

void RenderWorker::rebuildPreview(const RenderInputs &inputs) {
  if (!inputs.hasFull() || !inputs.preview) return;
  // makePreview rewrites the preview image, which the worker may be reading.
  waitIdle();
  builtPreviewMaxEdge_ = inputs.previewMaxEdge;
  makePreview(*inputs.full, inputs.previewMaxEdge, *inputs.preview);
  request(inputs);
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
  while (!stopping_) {
    bool recolorOnly = false;
    RenderInputs inputs;
    {
      std::unique_lock<std::mutex> lock(schedule_.mutex);
      schedule_.cv.wait(lock, [this] {
        return stopping_.load() || (schedule_.pending.load() && !schedule_.exporting);
      });
      if (stopping_) break;
      recolorOnly = schedule_.recolorPending;
      schedule_.recolorPending = false;
      schedule_.pending = false;
      schedule_.busy = true;
      inputs = inputs_;
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
      if (!inputs.hasChain()) from = inputs.workingSpace;
      else from = outputSpace();
      to = inputs.previewSpace;
      publish(std::move(img), from, to);
      continue;
    }
    if (!inputs.hasChain() || !inputs.hasPreview()) continue;
    const int gen = renderContext().beginPass();
    status_->report("Rendering...");
    Image out;
    const int longEdge = std::max(inputs.preview->w, inputs.preview->h);
    if (longEdge > kDraftMaxEdge) {
      Image draftInput;
      if (makePreview(*inputs.preview, kDraftMaxEdge, draftInput)) {
        const OfxStatus draftSt = renderer.render(*inputs.chain, plugins(), draftInput, out, gen, true,
                                                  passRenderScale(inputs.projectLongEdge(), draftInput));
        if (!renderContext().isCurrent(gen)) continue;
        if (draftSt == kOfxStatOK) publish(std::move(out), outputSpace(), inputs.previewSpace);
      }
    }
    if (!renderContext().isCurrent(gen)) continue;
    const OfxStatus st = renderer.render(*inputs.chain, plugins(), *inputs.preview, out, gen, false,
                                         passRenderScale(inputs.projectLongEdge(), *inputs.preview));
    if (!renderContext().isCurrent(gen)) continue;
    if (st == kOfxStatOK) {
      const int ow = out.w, oh = out.h;
      publish(std::move(out), outputSpace(), inputs.previewSpace);
      status_->report(std::to_string(ow) + "×" + std::to_string(oh) + " preview");
    } else {
      status_->report("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}
