#include "RenderScheduler.h"

#include "ChainRenderer.h"
#include "ofx/OfxHost.h"
#include "perf.h"

#include <algorithm>

// Long-edge cap for the fast interactive draft pass. The full pass runs at the
// preview size, so a draft is only built when the preview is larger than this.
static constexpr int kDraftMaxEdge = 1280;

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

static void publishDisplay(RenderState &rs, Image img, ColorSpace space) {
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(img, space, rgba);
  std::lock_guard<std::mutex> lock(rs.displayMutex);
  rs.display = std::move(img);
  rs.displayRGBA = std::move(rgba);
  rs.displayDirty = true;
}

static void showSourcePreview(App &app) {
  if (app.doc.preview.px.empty()) return;
  publishDisplay(app.render, app.doc.preview, linearWorkingSpace(app.doc.inputSpace));
}

void waitRenderIdle(App &app) {
  ++gLatestGen;
  app.render.schedule.waitIdle();
}

void scheduleRender(App &app) {
  if (app.chain.nodes.empty() || app.doc.preview.px.empty()) {
    showSourcePreview(app);
    return;
  }
  ++gLatestGen;
  {
    std::lock_guard<std::mutex> lock(app.render.schedule.mutex);
    app.render.schedule.pending = true;
  }
  app.render.schedule.cv.notify_one();
}

void rebuildPreview(App &app) {
  if (app.doc.full.px.empty()) return;
  waitRenderIdle(app);  // makePreview rewrites app.doc.preview, which the worker may be reading
  const int maxEdge = kPreviewRes[static_cast<int>(app.gui.previewRes)].maxEdge;
  makePreview(app.doc.full, maxEdge, app.doc.preview);
  scheduleRender(app);
}

static void uploadTextureRGBA(App &app, const unsigned char *rgba, int w, int h) {
  PerfScope _ps("uploadTextureRGBA");
  app.tex.upload(rgba, w, h);
}

void scheduleDisplayRecolor(App &app) {
  ++gLatestGen;
  std::lock_guard<std::mutex> lock(app.render.schedule.mutex);
  app.render.schedule.recolorPending = true;
  app.render.schedule.pending = true;
  app.render.schedule.cv.notify_one();
}

void pumpDisplayUpload(App &app) {
  std::lock_guard<std::mutex> lock(app.render.displayMutex);
  if (app.render.displayDirty && !app.render.displayRGBA.empty() && app.render.display.w > 0 &&
      app.render.display.h > 0) {
    uploadTextureRGBA(app, app.render.displayRGBA.data(), app.render.display.w, app.render.display.h);
    app.render.displayDirty = false;
  }
}

void renderWorker(App *app) {
  ChainRenderer renderer;
  while (!app->quit) {
    bool recolorOnly = false;
    {
      std::unique_lock<std::mutex> lock(app->render.schedule.mutex);
      app->render.schedule.cv.wait(lock, [&] {
        return app->quit || (app->render.schedule.pending.load() && !app->render.schedule.exporting);
      });
      if (app->quit) break;
      recolorOnly = app->render.schedule.recolorPending;
      app->render.schedule.recolorPending = false;
      app->render.schedule.pending = false;
      app->render.schedule.busy = true;
    }
    RenderSchedule::Guard busy(&app->render.schedule);
    if (recolorOnly) {
      Image img;
      ColorSpace space;
      {
        std::lock_guard<std::mutex> lock(app->render.displayMutex);
        if (app->render.display.px.empty()) continue;
        img = app->render.display;
        if (app->chain.nodes.empty()) space = linearWorkingSpace(app->doc.inputSpace);
        else space = app->outputTag.load();
      }
      publishDisplay(app->render, std::move(img), space);
      continue;
    }
    if (app->chain.nodes.empty() || app->doc.preview.px.empty()) continue;
    const int gen = ++gLatestGen;
    app->setStatus("Rendering...");
    Image out;
    const int longEdge = std::max(app->doc.preview.w, app->doc.preview.h);
    if (longEdge > kDraftMaxEdge) {
      Image draftInput;
      if (makePreview(app->doc.preview, kDraftMaxEdge, draftInput)) {
        const OfxStatus draftSt = renderer.render(app->chain, gPlugins, draftInput, out, gen, true);
        if (gen != gLatestGen) continue;
        if (draftSt == kOfxStatOK) {
          const ColorSpace space = app->outputTag;
          publishDisplay(app->render, std::move(out), space);
        }
      }
    }
    if (gen != gLatestGen) continue;
    const OfxStatus st = renderer.render(app->chain, gPlugins, app->doc.preview, out, gen, false);
    if (gen != gLatestGen) continue;
    if (st == kOfxStatOK) {
      const ColorSpace space = app->outputTag;
      const int ow = out.w, oh = out.h;
      publishDisplay(app->render, std::move(out), space);
      app->setStatus(std::to_string(ow) + "×" + std::to_string(oh) + " preview");
    } else {
      app->setStatus("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}
