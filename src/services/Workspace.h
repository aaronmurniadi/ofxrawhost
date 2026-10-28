// The running host's state, without the window and without the preview texture.
//
// Everything a command or a panel may touch lives here: the open document, the
// interface settings, the chain, the filmstrip, the update check, the status
// line, and the two workers. The composition root (app/AppState.h) adds the
// window and the texture on top, and only the interface code that draws the
// preview sees those.
//
// Prefer ManagerSession (via session()) for new manager and chain APIs. Do not
// grow this aggregate.

#pragma once

#include "core/StatusSink.h"
#include "engine/ExportWorker.h"
#include "engine/RenderWorker.h"
#include "model/Chain.h"
#include "model/Document.h"
#include "model/GuiState.h"
#include "services/Filmstrip.h"
#include "services/ManagerSession.h"
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

  ManagerSession session() {
    return ManagerSession{document, gui, chain, filmstrip, status, render, exporter, opener};
  }

  void reportStatus(const std::string &text) { status.report(text); }
  std::string statusText() const { return status.text(); }

  void scheduleRender() { session().scheduleRender(); }
  void scheduleRecolor() { session().scheduleRecolor(); }
  void rebuildPreviewImage() { session().rebuildPreviewImage(); }
};
