#include "RenderPipeline.h"

#include <GLFW/glfw3.h>

#include <vector>

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

static void showSourcePreview(App &app) {
  if (app.preview.px.empty()) return;
  const ColorSpace space = linearWorkingSpace(app.inputSpace);
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(app.preview, space, rgba);
  std::lock_guard<std::mutex> lock(app.displayMutex);
  app.display = app.preview;
  app.displayRGBA = std::move(rgba);
  app.displayDirty = true;
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
  const int maxEdge = kPreviewRes[std::clamp(app.previewRes, 0, kPreviewResCount - 1)].maxEdge;
  makePreview(app.full, maxEdge, app.preview);
  scheduleRender(app);
}

static void uploadTextureRGBA(App &app, const unsigned char *rgba, int w, int h) {
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

void uploadTexture(App &app, const Image &img) {
  const ColorSpace space =
      app.nodes.empty() ? linearWorkingSpace(app.inputSpace) : outputSpace(app.outputIndex);
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(img, space, rgba);
  uploadTextureRGBA(app, rgba.data(), img.w, img.h);
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

OfxStatus renderChain(App &app, const Image &src, Image &out, int gen) {
  static thread_local Image cur, next;
  cur.w = src.w;
  cur.h = src.h;
  cur.px = src.px;
  for (size_t i = 0; i < app.nodes.size(); ++i) {
    Node &n = app.nodes[i];
    if (!n.enabled) continue;
    if (!n.instance) return kOfxStatFailed;
    next.w = cur.w;
    next.h = cur.h;
    if (next.px.size() != cur.px.size()) next.px.resize(cur.px.size());
    const OfxStatus st =
        renderEffect(gPlugins[n.pluginIndex].plugin, n.instance.get(), cur.px.data(), next.px.data(), cur.w, cur.h, gen);
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
        space = app->nodes.empty() ? linearWorkingSpace(app->inputSpace) : outputSpace(app->outputIndex);
      }
      std::vector<unsigned char> rgba;
      toDisplayRGBA8(img, space, rgba);
      std::lock_guard<std::mutex> lock(app->displayMutex);
      app->displayRGBA = std::move(rgba);
      app->displayDirty = true;
      continue;
    }
    if (app->nodes.empty() || app->preview.px.empty()) continue;
    const int gen = ++gLatestGen;
    const int pw = app->preview.w;
    const int ph = app->preview.h;
    app->setStatus("Rendering...");
    Image out;
    const OfxStatus st = renderChain(*app, app->preview, out, gen);
    if (gen != gLatestGen) continue;
    if (st == kOfxStatOK) {
      const ColorSpace space = outputSpace(app->outputIndex);
      std::vector<unsigned char> rgba;
      toDisplayRGBA8(out, space, rgba);
      std::lock_guard<std::mutex> lock(app->displayMutex);
      app->display = std::move(out);
      app->displayRGBA = std::move(rgba);
      app->displayDirty = true;
      app->displayGen = gen;
      app->setStatus(std::to_string(pw) + "×" + std::to_string(ph) + " preview");
    } else {
      app->setStatus("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}
