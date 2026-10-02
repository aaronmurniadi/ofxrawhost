#pragma once

#include "AppState.h"
#include "ofxCore.h"

// Runs the enabled nodes in order. Owns the two ping-pong buffers, so one caller
// can render many times without a thread_local. Give each rendering thread its own.
struct ChainRenderer {
  Image cur, next;
  void *mtl[2] = {nullptr, nullptr};  // id<MTLBuffer> chain ping-pong (may stay null)
  size_t mtlBytes[2] = {0, 0};        // capacities of mtl[0] and mtl[1]
  ChainRenderer() = default;
  ~ChainRenderer();
  ChainRenderer(const ChainRenderer &) = delete;
  ChainRenderer &operator=(const ChainRenderer &) = delete;
  OfxStatus render(App &app, const Image &src, Image &out, int gen);
};

void waitRenderIdle(App &app);
void scheduleRender(App &app);
void scheduleDisplayRecolor(App &app);
void rebuildPreview(App &app);
void pumpDisplayUpload(App &app);
void renderWorker(App *app);
