#pragma once

#include "AppState.h"

#include <string>

void openPath(App &app, const std::string &path);
// Opens dir as the workspace; returns false if it is not a directory.
bool openWorkspace(App &app, const std::string &dir);
bool canExport(const App &app);
std::string defaultExportName(const App &app);
// Appends the format extension when outPath has none, then exports.
void doExport(App &app, const std::string &outPath);

// Sets the space the chain is told the image is in, for the first plugin, and
// re-runs the chain walk that gives every later plugin the upstream space.
void setInputSpace(App &app, int index);
// Sets the space the preview is encoded in, and refreshes the cached display.
void setPreviewSpace(App &app, int index);
// Sets the color space of the exported file and the rendering intent of the
// conversion into it. It does not touch the preview or the chain.
void setExportSpace(App &app, int index);
void setExportIntent(App &app, int intentIndex);
// Sets the preview resolution and rebuilds the preview image (waits out the worker).
void setPreviewRes(App &app, int index);
