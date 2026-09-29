#pragma once

#include "AppState.h"

#include <string>

void openPath(App &app, const std::string &path);
void openWorkspace(App &app, const std::string &dir);
void doExport(App &app);
