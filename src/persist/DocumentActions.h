#pragma once

#include "AppState.h"
#include "persist/ProjectPersist.h"

PersistGui captureGui(const App &app);
void applyGui(App &app, const PersistGui &g);
void saveCurrentInputSidecar(App &app);
void persistWorkspace(App &app);
void openWorkspace(App &app, const std::string &dir);
void openPath(App &app, const std::string &path, bool applySidecar = true);
void doExport(App &app);
