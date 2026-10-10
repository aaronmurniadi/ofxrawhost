#pragma once

#include "AppState.h"
#include "persist/ProjectPersist.h"

PersistGui captureGui(const App &app);
void applyGui(App &app, const PersistGui &g);
// Update preferences are captured and applied on their own, because only the
// settings file records them.
PersistUpdates captureUpdates(const App &app);
void applyUpdates(App &app, const PersistUpdates &u);
void saveCurrentInputSidecar(App &app);
void persistWorkspace(App &app);
