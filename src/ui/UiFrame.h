#pragma once

#include "app/AppState.h"

void drawPluginsPanel(Workspace &app);
void drawParametersPanel(Workspace &app);
void drawPreviewPanel(App &app);
void drawFilmstripPanel(Workspace &app);
void drawUiFrame(App &app);
// Prompts for a destination file, then exports the current image through the node chain.
void exportImage(Workspace &app);
