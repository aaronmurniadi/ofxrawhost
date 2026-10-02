#include "RenderPipeline.h"
#include "perf.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <vector>

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

static void publishDisplay(App &app, Image img, ColorSpace space) {
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(img, space, rgba);
  std::lock_guard<std::mutex> lock(app.displayMutex);
  app.display = std::move(img);
  app.displayRGBA = std::move(rgba);
  app.displayDirty = true;
}

static void showSourcePreview(App &app) {
  if (app.preview.px.empty()) return;
  publishDisplay(app, app.preview, linearWorkingSpace(app.inputSpace));
}

void waitRenderIdle(App &app) {
  ++gLatestGen;
  app.render.waitIdle();
}

void scheduleRender(App &app) {
  if (app.nodes.empty() || app.preview.px.empty()) {
    showSourcePreview(app);
    return;
  }
  ++gLatestGen;
  {
    std::lock_guard<std::mutex> lock(app.render.mutex);
    if (!app.render.busy && !app.render.exporting) {
      for (auto &n : app.nodes)
        if (n.instance) n.instance->setInputSize(app.preview.w, app.preview.h);
    }
    app.render.pending = true;
  }
  app.render.cv.notify_one();
}

void rebuildPreview(App &app) {
  if (app.full.px.empty()) return;
  waitRenderIdle(app);  // makePreview rewrites app.preview, which the worker may be reading
  const int maxEdge = kPreviewRes[static_cast<int>(app.previewRes)].maxEdge;
  makePreview(app.full, maxEdge, app.preview);
  scheduleRender(app);
}

static void uploadTextureRGBA(App &app, const unsigned char *rgba, int w, int h) {
  PerfScope _ps("uploadTextureRGBA");
  app.tex.upload(rgba, w, h);
}

void scheduleDisplayRecolor(App &app) {
  ++gLatestGen;
  std::lock_guard<std::mutex> lock(app.render.mutex);
  app.render.recolorPending = true;
  app.render.pending = true;
  app.render.cv.notify_one();
}

void pumpDisplayUpload(App &app) {
  std::lock_guard<std::mutex> lock(app.displayMutex);
  if (app.displayDirty && !app.displayRGBA.empty() && app.display.w > 0 && app.display.h > 0) {
    uploadTextureRGBA(app, app.displayRGBA.data(), app.display.w, app.display.h);
    app.displayDirty = false;
  }
}


static bool anyEnabledNode(const App &app) {
  for (const auto &n : app.nodes)
    if (n.enabled) return true;
  return false;
}

OfxStatus ChainRenderer::render(App &app, const Image &src, Image &out, int gen) {
  if (!anyEnabledNode(app)) {
    out = src;
    return kOfxStatOK;
  }

  cur.w = src.w;
  cur.h = src.h;
  cur.px = src.px;
  for (size_t i = 0; i < app.nodes.size(); ++i) {
    Node &n = app.nodes[i];
    if (!n.enabled) continue;
    if (!n.instance) return kOfxStatFailed;
    OfxPlugin *plugin = gPlugins[n.pluginIndex].plugin;
    int ow = cur.w, oh = cur.h;
    queryOutputSize(plugin, n.instance.get(), cur.w, cur.h, &ow, &oh);
    next.w = ow;
    next.h = oh;
    const size_t need = (size_t)ow * oh * 4;
    if (next.px.size() < need) next.px.resize(need);
    const auto t0 = std::chrono::steady_clock::now();
    const OfxStatus st =
        renderEffect(plugin, n.instance.get(), cur.px.data(), next.px.data(), cur.w, cur.h, ow, oh, gen);
    const auto t1 = std::chrono::steady_clock::now();
    perfLog(("node: " + gPlugins[n.pluginIndex].label).c_str(),
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (st != kOfxStatOK) return st;
    if (gen != 0 && gen != gLatestGen) return kOfxStatFailed;
    cur.swap(next);
  }
  out = std::move(cur);
  return kOfxStatOK;
}

void renderWorker(App *app) {
  ChainRenderer renderer;
  while (!app->quit) {
    bool recolorOnly = false;
    {
      std::unique_lock<std::mutex> lock(app->render.mutex);
      app->render.cv.wait(lock,
                          [&] { return app->quit || (app->render.pending.load() && !app->render.exporting); });
      if (app->quit) break;
      recolorOnly = app->render.recolorPending;
      app->render.recolorPending = false;
      app->render.pending = false;
      app->render.busy = true;
    }
    RenderSchedule::Guard busy(&app->render);
    if (recolorOnly) {
      Image img;
      ColorSpace space;
      {
        std::lock_guard<std::mutex> lock(app->displayMutex);
        if (app->display.px.empty()) continue;
        img = app->display;
        space = app->nodes.empty() ? linearWorkingSpace(app->inputSpace) : app->outputTag.load();
      }
      publishDisplay(*app, std::move(img), space);
      continue;
    }
    if (app->nodes.empty() || app->preview.px.empty()) continue;
    const int gen = ++gLatestGen;
    app->setStatus("Rendering...");
    Image out;
    const OfxStatus st = renderer.render(*app, app->preview, out, gen);
    if (gen != gLatestGen) continue;
    if (st == kOfxStatOK) {
      const ColorSpace space = app->outputTag;
      const int ow = out.w, oh = out.h;
      publishDisplay(*app, std::move(out), space);
      app->setStatus(std::to_string(ow) + "×" + std::to_string(oh) + " preview");
    } else {
      app->setStatus("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}
