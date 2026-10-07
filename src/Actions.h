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

// Sets the output color tag and refreshes the cached display (no plugin re-render).
void setOutputTag(App &app, int index);
// Sets the preview resolution and rebuilds the preview image (waits out the worker).
void setPreviewRes(App &app, int index);
