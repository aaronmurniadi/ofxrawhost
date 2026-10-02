#include "RenderPipeline.h"
#include "ofx/OfxMetal.h"
#include "perf.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstring>
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

// Grows a chain-owned Metal buffer in place; releases the old one when it is too small.
static void ensureChainBuffer(void *&slot, size_t &haveBytes, size_t wantBytes) {
  if (slot && haveBytes >= wantBytes) return;
  if (slot) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(slot));
  slot = ofxMetalBufferCreate(wantBytes);
  haveBytes = slot ? wantBytes : 0;
}

ChainRenderer::~ChainRenderer() {
  for (int i = 0; i < 2; ++i)
    if (mtl[i]) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(mtl[i]));
}

OfxStatus ChainRenderer::render(App &app, const Image &src, Image &out, int gen) {
  if (!anyEnabledNode(app)) {
    out = src;
    return kOfxStatOK;
  }

  cur.w = src.w;
  cur.h = src.h;
  cur.px = src.px;
  int cw = src.w, ch = src.h;
  float *cpuIn = cur.px.data();
  void *gpuIn = nullptr;  // when set, the pixels live in this MTLBuffer (cw x ch)
  int gpuDst = 0;         // which chain buffer the next GPU node writes into
  bool metalUsed = false;
  OfxStatus st = kOfxStatOK;

  for (size_t i = 0; i < app.nodes.size(); ++i) {
    Node &n = app.nodes[i];
    if (!n.enabled) continue;
    if (!n.instance) return kOfxStatFailed;
    OfxPlugin *plugin = gPlugins[n.pluginIndex].plugin;
    int ow = cw, oh = ch;
    queryOutputSize(plugin, n.instance.get(), cw, ch, &ow, &oh);
    const auto t0 = std::chrono::steady_clock::now();
    if (effectUsesMetal(n.instance.get())) {
      // Keep the frame on the GPU: feed the previous node's output buffer straight
      // in and write the next one. No per-node copy and no per-node GPU stall.
      void *dstBuf = mtl[gpuDst];
      ensureChainBuffer(dstBuf, mtlBytes[gpuDst], (size_t)ow * oh * 4 * sizeof(float));
      mtl[gpuDst] = dstBuf;
      if (!dstBuf) return kOfxStatErrMemory;
      float *srcCpu = gpuIn ? nullptr : cpuIn;
      st = renderEffect(plugin, n.instance.get(), srcCpu, nullptr, cw, ch, ow, oh, gen, gpuIn, dstBuf);
      if (st != kOfxStatOK) break;
      gpuIn = dstBuf;
      cpuIn = nullptr;
      metalUsed = true;
      gpuDst = 1 - gpuDst;
    } else {
      if (gpuIn) {
        // The lazy copy is a Skyline: the GPU result must be ready before the CPU read.
        ofxMetalSync();
        cur.w = cw;
        cur.h = ch;
        const size_t need = (size_t)cw * ch * 4;
        if (cur.px.size() < need) cur.px.resize(need);
        const void *p = ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(gpuIn));
        if (!p) return kOfxStatFailed;
        std::memcpy(cur.px.data(), p, need * sizeof(float));
        gpuIn = nullptr;
        cpuIn = cur.px.data();
      }
      next.w = ow;
      next.h = oh;
      const size_t need = (size_t)ow * oh * 4;
      if (next.px.size() < need) next.px.resize(need);
      st = renderEffect(plugin, n.instance.get(), cpuIn, next.px.data(), cw, ch, ow, oh, gen);
      if (st != kOfxStatOK) break;
      cur.swap(next);
      cpuIn = cur.px.data();
    }
    const auto t1 = std::chrono::steady_clock::now();
    perfLog(("node: " + gPlugins[n.pluginIndex].label).c_str(),
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (gen != 0 && gen != gLatestGen) {
      st = kOfxStatFailed;
      break;
    }
    cw = ow;
    ch = oh;
  }

  if (st != kOfxStatOK) {
    if (metalUsed) ofxMetalSync();  // let queued GPU work finish before the buffers are reused
    return st;
  }
  if (gpuIn) {
    ofxMetalSync();
    next.w = cw;
    next.h = ch;
    const size_t need = (size_t)cw * ch * 4;
    if (next.px.size() < need) next.px.resize(need);
    const void *p = ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(gpuIn));
    if (!p) return kOfxStatFailed;
    std::memcpy(next.px.data(), p, need * sizeof(float));
    out = std::move(next);
    return kOfxStatOK;
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
