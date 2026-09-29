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
  std::unique_lock<std::mutex> lock(app.renderMutex);
  app.renderPending = false;
}

void scheduleRender(App &app) {
  if (app.nodes.empty() || app.preview.px.empty()) {
    showSourcePreview(app);
    return;
  }
  for (auto &n : app.nodes) {
    if (n.instance) {
      n.instance->w = app.preview.w;
      n.instance->h = app.preview.h;
    }
  }
  ++gLatestGen;
  app.renderPending = true;
  app.renderCv.notify_one();
}

void rebuildPreview(App &app) {
  if (app.full.px.empty()) return;
  const int maxEdge = kPreviewRes[static_cast<int>(app.previewRes)].maxEdge;
  makePreview(app.full, maxEdge, app.preview);
  scheduleRender(app);
}

static void uploadTextureRGBA(App &app, const unsigned char *rgba, int w, int h) {
  PerfScope _ps("uploadTextureRGBA");
  if (!rgba || w <= 0 || h <= 0) return;
  if (!app.tex) glGenTextures(1, &app.tex);
  glBindTexture(GL_TEXTURE_2D, app.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (app.texW != w || app.texH != h) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    app.texW = w;
    app.texH = h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
}

void scheduleDisplayRecolor(App &app) {
  ++gLatestGen;
  std::lock_guard<std::mutex> lock(app.renderMutex);
  app.displayRecolorPending = true;
  app.renderPending = true;
  app.renderCv.notify_one();
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

OfxStatus renderChain(App &app, const Image &src, Image &out, int gen) {
  static thread_local Image cur, next;

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
  while (!app->quit) {
    bool recolorOnly = false;
    {
      std::unique_lock<std::mutex> lock(app->renderMutex);
      app->renderCv.wait(lock, [&] { return app->quit || app->renderPending.load(); });
      if (app->quit) break;
      recolorOnly = app->displayRecolorPending;
      app->displayRecolorPending = false;
      app->renderPending = false;
    }
    if (recolorOnly) {
      Image img;
      ColorSpace space;
      {
        std::lock_guard<std::mutex> lock(app->displayMutex);
        if (app->display.px.empty()) continue;
        img = app->display;
        space = app->nodes.empty() ? linearWorkingSpace(app->inputSpace) : app->outputTag;
      }
      publishDisplay(*app, std::move(img), space);
      continue;
    }
    if (app->nodes.empty() || app->preview.px.empty()) continue;
    const int gen = ++gLatestGen;
    app->setStatus("Rendering...");
    Image out;
    const OfxStatus st = renderChain(*app, app->preview, out, gen);
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
