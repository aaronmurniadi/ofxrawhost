#include "services/managers/document/DocumentManager.h"

#include "codecs/ImageIO.h"
#include "engine/RenderWorker.h"
#include "persist/ProjectPersist.h"
#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/ChainIO.h"
#include "services/Filmstrip.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "services/StateMapping.h"
#include "services/Workspace.h"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {

// Moves a decoded image into the document. Pure state: the caller has already
// stopped the render worker and decided what to do with the chain.
void adoptDocument(DocumentState &document, const std::string &path, Image &&img, ColorSpace detected,
                   ColorSpace working, const ImageMeta &meta) {
  document.path = path;
  document.full = std::move(img);
  document.fileSpace = detected;
  document.workingSpace = working;
  document.inputSpace = working;
  document.meta = meta;
}

// A new image opens fit in view, so the zoom and pan of the previous one do not
// carry over to a different frame size.
void resetViewFor(GuiState &gui) {
  gui.previewZoom = 1.0f;
  gui.previewPanX = 0.0f;
  gui.previewPanY = 0.0f;
}

class DocumentManagerImpl : public DocumentManager {
 public:
  DocumentManagerImpl(Workspace &workspace, SidecarDao &sidecar, WorkspaceProjectDao &project)
      : workspace_(workspace), sidecar_(sidecar), project_(project) {}

  void openPath(const std::string &path) override {
    if (isHostMetadataPath(path)) {
      workspace_.reportStatus("Sidecar files (.ofxrawhost.json) are not images — open the image file instead.");
      return;
    }
    // The outgoing image keeps its own chain, so write its sidecar before the
    // document is replaced.
    if (!workspace_.document.path.empty() && workspace_.document.path != path) saveCurrentInputSidecar();

    // A saved chain and the chain in place are both work, so the user picks which
    // one this image opens with. The open resumes when they answer.
    if (savedChainNeedsAnswer(path)) {
      workspace_.gui.pendingOpenPath = path;
      workspace_.gui.showSidecarPrompt = true;
      return;
    }
    startDecode(path, /*useSavedChain=*/true);
  }

  bool openWorkspace(const std::string &dir) override {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      workspace_.reportStatus("Not a directory");
      return false;
    }
    saveCurrentInputSidecar();
    persistWorkspace();
    workspace_.document.workspaceDir = fs::weakly_canonical(fs::path(dir), ec).string();
    refreshFilmstrip(workspace_.filmstrip, workspace_.document.workspaceDir, workspace_.document.path);
    // The project records which image was open. Its chain lives in the sidecar of
    // that image, and the interface state lives in the settings file.
    std::string activeRel;
    project_.loadProject(workspace_.document.workspaceDir, activeRel);
    std::string toOpen;
    if (!activeRel.empty()) {
      fs::path p = fs::path(workspace_.document.workspaceDir) / activeRel;
      if (fs::is_regular_file(p, ec)) toOpen = p.string();
    }
    if (toOpen.empty() && !workspace_.filmstrip.entries.empty()) toOpen = workspace_.filmstrip.entries[0].path;
    if (!toOpen.empty())
      openPath(toOpen);
    else
      workspace_.reportStatus("Directory: " + fs::path(workspace_.document.workspaceDir).filename().string() +
                              " (no images)");
    // An open that waits for the user to answer records the active image itself
    // once it finishes, so a half-finished open leaves the project alone.
    if (workspace_.gui.pendingOpenPath.empty()) persistWorkspace();
    workspace_.gui.showFilmstrip = true;
    return true;
  }

  void resolvePendingOpen(bool useSavedChain) override {
    std::string path = std::move(workspace_.gui.pendingOpenPath);
    workspace_.gui.pendingOpenPath.clear();
    workspace_.gui.showSidecarPrompt = false;
    if (path.empty()) return;
    startDecode(path, useSavedChain);
  }

  bool pumpOpen() override {
    DecodedImage decoded;
    if (!workspace_.opener.take(decoded)) return false;
    if (!decoded.ok) {
      workspace_.reportStatus("Could not decode " + fs::path(decoded.path).filename().string());
      return true;
    }
    applyDecoded(std::move(decoded));
    return true;
  }

  void saveCurrentInputSidecar() override {
    if (workspace_.document.path.empty()) return;
    sidecar_.save(workspace_.document.path, workspace_.document.fileSpace, workspace_.document.inputSpace,
                  captureChain(workspace_.chain));
  }

  void persistWorkspace() override {
    project_.saveSettings(captureGui(workspace_.gui), captureAppearance(workspace_.gui),
                          captureUpdates(workspace_.gui));
    if (workspace_.document.workspaceDir.empty()) return;
    project_.saveActiveImage(workspace_.document.workspaceDir, workspace_.document.path);
  }

 private:
  // Applies a sidecar to the image now open: the input space the user picked for
  // the chain, and the chain itself.
  void applySidecarFor(const PersistSidecar &sidecar) {
    workspace_.document.inputSpace =
        colorSpaceFromName(sidecar.chainInputColorSpace, workspace_.document.workingSpace);
    applyChain(workspace_, sidecar.chain);
  }

  // The chain of the previous image stays in place when the new image has no
  // sidecar, and its noise model belonged to the previous camera, so seed it for
  // the camera now open. Sets the status when a profile was applied, which is the
  // only cue the user gets for it.
  void seedNoiseForOpenCamera(const ImageMeta &meta) {
    if (seedNoiseModelParams(workspace_) > 0)
      workspace_.reportStatus("Denoise (profiled): " + meta.cameraMaker + " " + meta.cameraModel + " ISO " +
                              std::to_string(meta.iso) + " profile applied");
  }

  // True when opening this image would throw away work the user has to choose
  // about: the image has a saved chain of its own, and the chain in place holds
  // plugins too, so the two cannot both survive the open.
  bool savedChainNeedsAnswer(const std::string &path) {
    if (workspace_.document.path.empty() || workspace_.document.path == path) return false;
    if (workspace_.chain.nodes.empty()) return false;
    PersistSidecar saved;
    return sidecar_.load(path, saved) && !saved.chain.nodes.empty();
  }

  // Applies a finished decode. Everything here is fast and runs on the interface
  // thread: the slow part was the decode, which the worker already did.
  void applyDecoded(DecodedImage &&decoded) {
    const std::string name = fs::path(decoded.path).filename().string();
    workspace_.render.waitIdle();  // worker reads inputSpace; stop it before swapping the image
    adoptDocument(workspace_.document, decoded.path, std::move(decoded.full), decoded.fileSpace,
                  decoded.workingSpace, decoded.meta);
    resetViewFor(workspace_.gui);
    workspace_.reportStatus("Loaded " + name + " (" + colorSpaceName(decoded.fileSpace) + ")");
    workspace_.filmstrip.index = filmstripIndexForPath(workspace_.filmstrip, decoded.path);

    PersistSidecar saved;
    if (!decoded.useSavedChain) {
      // The user chose the parameters of the image that was open, so they carry
      // into this image as they are.
      workspace_.reportStatus("Loaded " + name + " with the parameters of the previous image");
    } else if (sidecar_.load(decoded.path, saved)) {
      applySidecarFor(saved);
    } else {
      seedNoiseForOpenCamera(decoded.meta);
    }

    workspace_.rebuildPreviewImage();
    // The chain carries the document input space into its first plugin, and no
    // sidecar may have run, so re-derive it for the image that is now open.
    syncChainColorSpaces(workspace_);
    persistWorkspace();
  }

  // Starts the decode on the worker. The rest of the open happens when the result
  // is pumped in, so the window keeps drawing while a RAW develops.
  void startDecode(const std::string &path, bool useSavedChain) {
    const std::string name = fs::path(path).filename().string();
    if (!workspace_.opener.start(path, useSavedChain)) {
      workspace_.reportStatus("Still opening " + fs::path(workspace_.opener.path()).filename().string());
      return;
    }
    workspace_.reportStatus("Opening " + name + "\xE2\x80\xA6");
  }

  Workspace &workspace_;
  SidecarDao &sidecar_;
  WorkspaceProjectDao &project_;
};

}  // namespace

std::unique_ptr<DocumentManager> createDocumentManager(Workspace &workspace, SidecarDao &sidecar,
                                                       WorkspaceProjectDao &project) {
  return std::make_unique<DocumentManagerImpl>(workspace, sidecar, project);
}
