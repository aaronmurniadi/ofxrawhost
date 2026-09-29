#pragma once

#include "AppState.h"

#include <string>

void openPath(App &app, const std::string &path);
// Opens dir as the workspace; returns false if it is not a directory.
bool openWorkspace(App &app, const std::string &dir);
void doExport(App &app);
