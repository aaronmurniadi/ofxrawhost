#pragma once

#include "AppState.h"

void drawLeftPanel(App &app);
void drawRightPanel(App &app);
void drawPreviewPanel(App &app);
void drawFilmstripPanel(App &app);
void drawUiFrame(App &app);
// Prompts for a destination file, then exports the current image through the node chain.
void exportImage(App &app);
