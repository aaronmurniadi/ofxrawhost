// Narrow session refs for managers and chain commands. Prefer this over
// Workspace& on new APIs so a caller that needs the document cannot also reach
// unrelated aggregate members through the same parameter.
#pragma once

#include "core/StatusSink.h"
#include "engine/ExportWorker.h"
#include "engine/RenderInputs.h"
#include "engine/RenderWorker.h"
#include "model/Chain.h"
#include "model/Document.h"
#include "model/GuiState.h"
#include "services/Filmstrip.h"
#include "services/OpenWorker.h"

#include <string>

struct ManagerSession {
  DocumentState &document;
  GuiState &gui;
  ChainState &chain;
  Filmstrip &filmstrip;
  StatusSink &status;
  RenderWorker &render;
  ExportWorker &exporter;
  OpenWorker &opener;

  void reportStatus(const std::string &text) { status.report(text); }

  RenderInputs renderInputs() {
    RenderInputs in;
    in.chain = &chain;
    in.full = &document.full;
    in.preview = &document.preview;
    in.workingSpace = document.workingSpace;
    in.previewSpace = gui.previewSpace;
    in.previewMaxEdge = previewMaxEdge(gui);
    return in;
  }

  void scheduleRender() { render.request(renderInputs()); }
  void scheduleRecolor() { render.recolor(renderInputs()); }
  void rebuildPreviewImage() { render.rebuildPreview(renderInputs()); }
};
