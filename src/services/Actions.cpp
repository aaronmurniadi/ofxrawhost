#include "services/Actions.h"

#include "model/Filmstrip.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "engine/RenderWorker.h"
#include "services/Session.h"
#include "imgio/ImageIO.h"
#include "persist/ProjectPersist.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {

// Moves a decoded image into the document. Pure state: the caller has already
// stopped the render worker and decided what to do with the chain.
void adoptDocument(DocumentState &doc, const std::string &path, Image &&img, ColorSpace detected,
                   ColorSpace working, const ImageMeta &meta) {
  doc.path = path;
  doc.full = std::move(img);
  doc.fileSpace = detected;
  doc.workingSpace = working;
  doc.inputSpace = working;
  doc.meta = meta;
}

// A new image opens fit in view, so the zoom and pan of the previous one do not
// carry over to a different frame size.
void resetViewFor(GuiState &gui) {
  gui.previewZoom = 1.0f;
  gui.previewPanX = 0.0f;
  gui.previewPanY = 0.0f;
}

// Restores the chain recorded in the image sidecar, or seeds the chain the
// workspace already had for the camera now open. Sets the status when a noise
// profile was applied, which is the only cue the user gets for it.
void restoreChainFor(Workspace &app, const std::string &path, const ImageMeta &meta) {
  PersistSidecar sc;
  if (loadInputSidecar(path, sc)) {
    // A sidecar that recorded a chain input space restores that choice.
    app.doc.inputSpace = colorSpaceFromName(sc.chainInputColorSpace, app.doc.workingSpace);
    applyChain(app, sc.chain);
    return;
  }
  // Without a sidecar the chain of the previous image stays in place, and its
  // noise model belonged to that camera, so seed it for the camera now open.
  if (seedNoiseModelParams(app) > 0)
    app.setStatus("Denoise (profiled): " + meta.cameraMaker + " " + meta.cameraModel + " ISO " +
                  std::to_string(meta.iso) + " profile applied");
}

}  // namespace

bool openWorkspace(Workspace &app, const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    app.setStatus("Not a directory");
    return false;
  }
  saveCurrentInputSidecar(app.doc, app.chain);
  persistWorkspace(app.doc, app.gui);
  app.doc.workspaceDir = fs::weakly_canonical(fs::path(dir), ec).string();
  refreshFilmstrip(app.filmstrip, app.doc.workspaceDir, app.doc.path);
  // The project records which image was open. Its chain lives in the sidecar of
  // that image, and the interface state lives in the settings file.
  std::string activeRel;
  loadWorkspaceProject(app.doc.workspaceDir, activeRel);
  std::string toOpen;
  if (!activeRel.empty()) {
    fs::path p = fs::path(app.doc.workspaceDir) / activeRel;
    if (fs::is_regular_file(p, ec)) toOpen = p.string();
  }
  if (toOpen.empty() && !app.filmstrip.entries.empty()) toOpen = app.filmstrip.entries[0].path;
  if (!toOpen.empty())
    openPath(app, toOpen);
  else
    app.setStatus("Directory: " + fs::path(app.doc.workspaceDir).filename().string() + " (no images)");
  persistWorkspace(app.doc, app.gui);
  app.gui.showFilmstrip = true;
  return true;
}

void openPath(Workspace &app, const std::string &path) {
  if (isHostMetadataPath(path)) {
    app.setStatus("Sidecar files (.ofxrawhost.json) are not images — open the image file instead.");
    return;
  }
  // The outgoing image keeps its own chain, so write its sidecar before the
  // document is replaced.
  if (!app.doc.path.empty() && app.doc.path != path) saveCurrentInputSidecar(app.doc, app.chain);

  Image img;
  ColorSpace detected = ColorSpace::LinearRec2020;
  ColorSpace working = ColorSpace::LinearRec709;
  ImageMeta meta;
  if (!loadImage(path, img, detected, working, &meta)) {
    app.setStatus("Could not decode " + fs::path(path).filename().string());
    return;
  }

  app.render.waitIdle();  // worker reads inputSpace; stop it before swapping the image
  adoptDocument(app.doc, path, std::move(img), detected, working, meta);
  resetViewFor(app.gui);
  app.setStatus("Loaded " + fs::path(path).filename().string() + " (" + colorSpaceName(detected) + ")");
  app.filmstrip.index = filmstripIndexForPath(app.filmstrip, path);

  restoreChainFor(app, path, meta);
  app.render.rebuildPreview();
  // The chain carries the document input space into its first plugin, and no
  // sidecar may have run, so re-derive it for the image that is now open.
  syncChainColorSpaces(app);
  persistWorkspace(app.doc, app.gui);
}

void setInputSpace(Workspace &app, int index) {
  app.doc.inputSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  // The chain carries this space into its first plugin, and a later plugin can
  // change what leaves the chain, so the whole walk and a render are needed.
  syncChainColorSpaces(app);
  app.render.request();
}

void setPreviewSpace(Workspace &app, int index) {
  // The preview is the destination of the last conversion: the chain output is
  // encoded into this space and shown as it is. Only the display conversion
  // changes, so a re-render is not needed.
  app.gui.previewSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  app.render.recolor();
}

void setExportSpace(Workspace &app, int index) {
  app.gui.exportSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
}

void setExportIntent(Workspace &app, int intentIndex) {
  app.gui.exportIntent = static_cast<RenderIntent>(std::clamp(intentIndex, 0, kRenderIntentCount - 1));
}

void setPreviewRes(Workspace &app, int index) {
  app.gui.previewRes = static_cast<PreviewRes>(std::clamp(index, 0, kPreviewResCount - 1));
  app.render.rebuildPreview();
}
