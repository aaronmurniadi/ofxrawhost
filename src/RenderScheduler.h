// Render scheduler: owns the worker loop and the request flags. This is the
// coordinator that ties the chain, the document, and the display buffer together,
// so it is the part of rendering that legitimately knows App.
#pragma once

#include "AppState.h"

void waitRenderIdle(App &app);
void scheduleRender(App &app);
void scheduleDisplayRecolor(App &app);
void rebuildPreview(App &app);
void pumpDisplayUpload(App &app);
void renderWorker(App *app);
