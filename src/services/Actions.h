// Document commands: open an image or a workspace, and set the color spaces the
// chain, the preview, and the export use. Export orchestration lives in Export.h.
#pragma once

#include "services/Workspace.h"

#include <string>

void openPath(Workspace &app, const std::string &path);
// Opens dir as the workspace; returns false if it is not a directory.
bool openWorkspace(Workspace &app, const std::string &dir);

// Sets the space the chain is told the image is in, for the first plugin, and
// re-runs the chain walk that gives every later plugin the upstream space.
void setInputSpace(Workspace &app, int index);
// Sets the space the preview is encoded in, and refreshes the cached display.
void setPreviewSpace(Workspace &app, int index);
// Sets the color space of the exported file and the rendering intent of the
// conversion into it. It does not touch the preview or the chain.
void setExportSpace(Workspace &app, int index);
void setExportIntent(Workspace &app, int intentIndex);
// Sets the preview resolution and rebuilds the preview image (waits out the worker).
void setPreviewRes(Workspace &app, int index);
