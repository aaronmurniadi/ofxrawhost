#pragma once

#include "AppState.h"

#include <string>

void openPath(App &app, const std::string &path, bool applySidecar = true);
void openWorkspace(App &app, const std::string &dir);
void doExport(App &app);
