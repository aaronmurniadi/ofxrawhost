// The running host's state, without the window and without the preview texture.
//
// Everything a command or a panel may touch lives here: the open document, the
// interface settings, the chain, the filmstrip, the update check, and the two
// workers. The composition root (app/AppState.h) adds the window and the
// texture on top, and only the interface code that draws the preview sees those.
//
// This is the unit services take instead of App, so a document command cannot
// reach a GL handle or the window even by accident.

#pragma once

#include "engine/ExportWorker.h"
#include "engine/RenderHost.h"
#include "engine/RenderWorker.h"
#include "model/Chain.h"
#include "model/Document.h"
#include "model/Filmstrip.h"
#include "model/GuiState.h"
#include "services/UpdateCheck.h"

#include <atomic>
#include <mutex>
#include <string>

struct Workspace;

// Maps RenderHost onto the Workspace members. Declared first so Workspace can
// hold one; the bodies follow the struct, because they need it to be complete.
class WorkspaceRenderHost : public RenderHost {
 public:
  explicit WorkspaceRenderHost(Workspace &workspace) : workspace_(workspace) {}

  const ChainState &chain() const override;
  const Image &fullImage() const override;
  const Image &previewImage() const override;
  Image &mutablePreviewImage() override;
  ColorSpace workingSpace() const override;
  ColorSpace outputSpace() const override;
  ColorSpace previewSpace() const override;
  int projectLongEdge() const override;
  int wantedPreviewMaxEdge() const override;
  bool quitting() const override;
  void reportStatus(const std::string &text) override;

 private:
  Workspace &workspace_;
};

struct Workspace {
  DocumentState doc;
  GuiState gui;
  ChainState chain;
  Filmstrip filmstrip;
  UpdateCheck updates;

  // Color space the plugin chain writes, derived by walking the chain: the first
  // plugin is told the document input space, and every later plugin is told the
  // space the previous plugin declares. Written on the UI thread while renders
  // are in flight, so it is atomic.
  std::atomic<ColorSpace> chainOutputSpace{ColorSpace::sRGB};
  std::atomic<bool> quit{false};  // app lifetime; stops both workers

  WorkspaceRenderHost renderHost{*this};
  RenderWorker render{renderHost};
  ExportWorker exporter;

  std::mutex statusMutex;
  std::string status = "Open an image. Source is fed to the plugin as scene-linear.";
  void setStatus(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status = s;
  }
  std::string getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
  }
};

inline const ChainState &WorkspaceRenderHost::chain() const { return workspace_.chain; }

inline const Image &WorkspaceRenderHost::fullImage() const { return workspace_.doc.full; }

inline const Image &WorkspaceRenderHost::previewImage() const { return workspace_.doc.preview; }

inline Image &WorkspaceRenderHost::mutablePreviewImage() { return workspace_.doc.preview; }

inline ColorSpace WorkspaceRenderHost::workingSpace() const { return workspace_.doc.workingSpace; }

inline ColorSpace WorkspaceRenderHost::outputSpace() const { return workspace_.chainOutputSpace.load(); }

inline ColorSpace WorkspaceRenderHost::previewSpace() const { return workspace_.gui.previewSpace; }

inline int WorkspaceRenderHost::projectLongEdge() const {
  const int w = workspace_.doc.full.w;
  const int h = workspace_.doc.full.h;
  if (w > h) return w;
  return h;
}

inline int WorkspaceRenderHost::wantedPreviewMaxEdge() const { return previewMaxEdge(workspace_.gui); }

inline bool WorkspaceRenderHost::quitting() const { return workspace_.quit.load(); }

inline void WorkspaceRenderHost::reportStatus(const std::string &text) { workspace_.setStatus(text); }
