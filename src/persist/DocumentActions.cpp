#include "persist/DocumentActions.h"

#include "NodeGraph.h"
#include "persist/ChainIO.h"
#include "ui/Themes.h"

#include <algorithm>

PersistGui captureGui(const App &app) {
  PersistGui g;
  g.outputIndex = static_cast<int>(app.outputTag.load());
  g.exportFormat = static_cast<int>(app.gui.exportFormat);
  g.jpegQuality = app.gui.jpegQuality;
  g.previewRes = static_cast<int>(app.gui.previewRes);
  g.themeIndex = app.gui.themeIndex;
  g.showLeft = app.gui.showLeft;
  g.showRight = app.gui.showRight;
  g.showFilmstrip = app.gui.showFilmstrip;
  return g;
}

void applyGui(App &app, const PersistGui &g) {
  app.outputTag = outputSpace(g.outputIndex);
  app.gui.exportFormat = static_cast<ExportFormat>(std::clamp(g.exportFormat, 0, 2));
  app.gui.jpegQuality = std::clamp(g.jpegQuality, 1, 100);
  app.gui.previewRes = static_cast<PreviewRes>(std::clamp(g.previewRes, 0, kPreviewResCount - 1));
  if (g.themeIndex >= 0 && g.themeIndex < themeCount()) app.gui.themeIndex = g.themeIndex;
  app.gui.showLeft = g.showLeft;
  app.gui.showRight = g.showRight;
  app.gui.showFilmstrip = g.showFilmstrip;
  app.gui.themeApplyPending = true;
}

void saveCurrentInputSidecar(App &app) {
  if (app.doc.path.empty()) return;
  saveInputSidecar(app.doc.path, app.doc.inputSpace, captureGui(app), captureChain(app));
}

void persistWorkspace(App &app) {
  if (app.doc.workspaceDir.empty()) return;
  const std::string active =
      app.doc.path.empty() ? std::string() : relativeToWorkspace(app.doc.workspaceDir, app.doc.path);
  saveWorkspaceProject(app.doc.workspaceDir, captureGui(app), active);
}
