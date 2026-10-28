#include "services/managers/document/OpenImageManager.h"

#include "codecs/ImageIO.h"
#include "engine/RenderWorker.h"
#include "persist/ProjectPersist.h"
#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/ChainIO.h"
#include "services/Filmstrip.h"
#include "services/ManagerSession.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "services/StateMapping.h"

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

class OpenImageManagerImpl : public OpenImageManager {
 public:
  OpenImageManagerImpl(ManagerSession session, SidecarDao &sidecar, WorkspaceProjectDao &project)
      : session_(session), sidecar_(sidecar), project_(project) {}

  void openPath(const std::string &path) override {
    if (isHostMetadataPath(path)) {
      session_.reportStatus("Sidecar files (.ofxrawhost.json) are not images — open the image file instead.");
      return;
    }
    // The outgoing image keeps its own chain, so write its sidecar before the
    // document is replaced.
    if (!session_.document.path.empty() && session_.document.path != path) saveCurrentInputSidecar();

    // A saved chain and the chain in place are both work, so the user picks which
    // one this image opens with. The open resumes when they answer.
    if (savedChainNeedsAnswer(path)) {
      session_.gui.pendingOpenPath = path;
      session_.gui.showSidecarPrompt = true;
      return;
    }
    startDecode(path, /*useSavedChain=*/true);
  }

  void resolvePendingOpen(bool useSavedChain) override {
    std::string path = std::move(session_.gui.pendingOpenPath);
    session_.gui.pendingOpenPath.clear();
    session_.gui.showSidecarPrompt = false;
    if (path.empty()) return;
    startDecode(path, useSavedChain);
  }

  bool pumpOpen() override {
    DecodedImage decoded;
    if (!session_.opener.take(decoded)) return false;
    if (!decoded.ok) {
      session_.reportStatus("Could not decode " + fs::path(decoded.path).filename().string());
      return true;
    }
    applyDecoded(std::move(decoded));
    return true;
  }

  void saveCurrentInputSidecar() override {
    if (session_.document.path.empty()) return;
    sidecar_.save(session_.document.path, session_.document.fileSpace, session_.document.inputSpace,
                  captureChain(session_.chain));
  }

  void persistWorkspace() override {
    project_.saveSettings(captureGui(session_.gui), captureAppearance(session_.gui),
                          captureUpdates(session_.gui));
    if (session_.document.workspaceDir.empty()) return;
    project_.saveActiveImage(session_.document.workspaceDir, session_.document.path);
  }

 private:
  // Applies a sidecar to the image now open: the input space the user picked for
  // the chain, and the chain itself.
  void applySidecarFor(const PersistSidecar &sidecar) {
    session_.document.inputSpace =
        colorSpaceFromName(sidecar.chainInputColorSpace, session_.document.workingSpace);
    applyChain(session_, sidecar.chain);
  }

  // The chain of the previous image stays in place when the new image has no
  // sidecar, and its noise model belonged to the previous camera, so seed it for
  // the camera now open. Sets the status when a profile was applied, which is the
  // only cue the user gets for it.
  void seedNoiseForOpenCamera(const ImageMeta &meta) {
    if (seedNoiseModelParams(session_) > 0)
      session_.reportStatus("Denoise (profiled): " + meta.cameraMaker + " " + meta.cameraModel + " ISO " +
                            std::to_string(meta.iso) + " profile applied");
  }

  // True when opening this image would throw away work the user has to choose
  // about: the image has a saved chain of its own, and the chain in place holds
  // plugins too, so the two cannot both survive the open.
  bool savedChainNeedsAnswer(const std::string &path) {
    if (session_.document.path.empty() || session_.document.path == path) return false;
    if (session_.chain.nodes.empty()) return false;
    PersistSidecar saved;
    return sidecar_.load(path, saved) && !saved.chain.nodes.empty();
  }

  // Applies a finished decode. Everything here is fast and runs on the interface
  // thread: the slow part was the decode, which the worker already did.
  void applyDecoded(DecodedImage &&decoded) {
    const std::string name = fs::path(decoded.path).filename().string();
    session_.render.waitIdle();  // worker reads inputSpace; stop it before swapping the image
    adoptDocument(session_.document, decoded.path, std::move(decoded.full), decoded.fileSpace,
                  decoded.workingSpace, decoded.meta);
    resetViewFor(session_.gui);
    session_.reportStatus("Loaded " + name + " (" + colorSpaceName(decoded.fileSpace) + ")");
    session_.filmstrip.index = filmstripIndexForPath(session_.filmstrip, decoded.path);

    PersistSidecar saved;
    if (!decoded.useSavedChain) {
      // The user chose the parameters of the image that was open, so they carry
      // into this image as they are.
      session_.reportStatus("Loaded " + name + " with the parameters of the previous image");
    } else if (sidecar_.load(decoded.path, saved)) {
      applySidecarFor(saved);
    } else {
      seedNoiseForOpenCamera(decoded.meta);
    }

    session_.rebuildPreviewImage();
    // The chain carries the document input space into its first plugin, and no
    // sidecar may have run, so re-derive it for the image that is now open.
    syncChainColorSpaces(session_);
    persistWorkspace();
  }

  // Starts the decode on the worker. The rest of the open happens when the result
  // is pumped in, so the window keeps drawing while a RAW develops.
  void startDecode(const std::string &path, bool useSavedChain) {
    const std::string name = fs::path(path).filename().string();
    if (!session_.opener.start(path, useSavedChain)) {
      session_.reportStatus("Still opening " + fs::path(session_.opener.path()).filename().string());
      return;
    }
    session_.reportStatus("Opening " + name + "...");
  }

  ManagerSession session_;
  SidecarDao &sidecar_;
  WorkspaceProjectDao &project_;
};

}  // namespace

std::unique_ptr<OpenImageManager> createOpenImageManager(ManagerSession session, SidecarDao &sidecar,
                                                         WorkspaceProjectDao &project) {
  return std::make_unique<OpenImageManagerImpl>(session, sidecar, project);
}
