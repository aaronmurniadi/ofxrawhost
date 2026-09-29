#pragma once

#include "AppState.h"
#include "ofxCore.h"

void waitRenderIdle(App &app);
void scheduleRender(App &app);
void scheduleDisplayRecolor(App &app);
void rebuildPreview(App &app);
void pumpDisplayUpload(App &app);
OfxStatus renderChain(App &app, const Image &src, Image &out, int gen);
void renderWorker(App *app);
