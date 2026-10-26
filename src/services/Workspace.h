// The running host's state, without the window and without the preview texture.
//
// Everything a command or a panel may touch lives here: the open document, the
// interface settings, the chain, the filmstrip, the update check, the status
// line, and the two workers. The composition root (app/AppState.h) adds the
// window and the texture on top, and only the interface code that draws the
// preview sees those.

#pragma once

#include "core/StatusSink.h"
#include "engine/ExportWorker.h"
#include "engine/RenderInputs.h"
#include "engine/RenderWorker.h"
#include "model/Chain.h"
#include "model/Document.h"
#include "services/Filmstrip.h"
#include "model/GuiState.h"
#include "services/OpenWorker.h"
#include "services/UpdateCheck.h"

#include <string>

struct Workspace {
  DocumentState document;
  GuiState gui;
  ChainState chain;
  Filmstrip filmstrip;
  UpdateCheck updates;

  // Everything that reports status writes here, and the sink is declared before
  // the workers so it outlives them.
  StatusSink status;
  RenderWorker render{status};
  ExportWorker exporter;
  // Decodes the next image off the interface thread. Pumped once per frame.
  OpenWorker opener;

  void reportStatus(const std::string &text) { status.report(text); }
  std::string statusText() const { return status.text(); }

  // The state one render pass reads. Captured per request, so the worker never
  // looks at these members while the interface writes them.
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
