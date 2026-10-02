#pragma once

#include "AppState.h"
#include "ofxCore.h"

// Runs the enabled nodes in order. Owns the two ping-pong buffers, so one caller
// can render many times without a thread_local. Give each rendering thread its own.
struct ChainRenderer {
  Image cur, next;
  OfxStatus render(App &app, const Image &src, Image &out, int gen);
};

void waitRenderIdle(App &app);
void scheduleRender(App &app);
void scheduleDisplayRecolor(App &app);
void rebuildPreview(App &app);
void pumpDisplayUpload(App &app);
void renderWorker(App *app);
