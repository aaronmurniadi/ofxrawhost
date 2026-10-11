#pragma once

#include "AppState.h"
#include "persist/ProjectPersist.h"

PersistGui captureGui(const App &app);
void applyGui(App &app, const PersistGui &g);
// The theme and the interface font size are app-level, so they are captured and
// applied on their own: a project or a sidecar never carries them, and opening
// one never changes them.
PersistAppearance captureAppearance(const App &app);
void applyAppearance(App &app, const PersistAppearance &appearance);
// Update preferences are captured and applied on their own, because only the
// settings file records them.
PersistUpdates captureUpdates(const App &app);
void applyUpdates(App &app, const PersistUpdates &u);
void saveCurrentInputSidecar(App &app);
void persistWorkspace(App &app);
