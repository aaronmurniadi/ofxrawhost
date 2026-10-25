// Session capture and restore: maps the live application state to and from the
// persisted shapes in persist/ProjectPersist.h. It is a service, not storage:
// persist/ reads and writes the files, and this module decides which state unit
// each field belongs to.
//
// Every entry point takes the narrow state unit it touches, never App. A caller
// that owns an App passes the unit it needs (app.gui, app.doc, app.chain).
#pragma once

#include "persist/ProjectPersist.h"

// The state units, forward-declared so this header does not pull in the
// composition root. Defined in AppState.h, Document.h, and Chain.h.
struct GuiState;
struct DocumentState;
struct ChainState;

PersistGui captureGui(const GuiState &gui);
void applyGui(GuiState &gui, const PersistGui &g);
// The theme and the interface font size are app-level, so they are captured and
// applied on their own: a project or a sidecar never carries them, and opening
// one never changes them.
PersistAppearance captureAppearance(const GuiState &gui);
void applyAppearance(GuiState &gui, const PersistAppearance &appearance);
// Update preferences are captured and applied on their own, because only the
// settings file records them.
PersistUpdates captureUpdates(const GuiState &gui);
void applyUpdates(GuiState &gui, const PersistUpdates &u);

// Writes the sidecar of the open image, if there is one. The chain travels with
// it, so the caller supplies both units.
void saveCurrentInputSidecar(const DocumentState &doc, const ChainState &chain);
// Writes the per-user settings file, and the workspace project when a workspace
// is open.
void persistWorkspace(const DocumentState &doc, const GuiState &gui);
