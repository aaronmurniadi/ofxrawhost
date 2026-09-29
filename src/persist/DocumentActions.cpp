#include "persist/DocumentActions.h"

#include "NodeGraph.h"
#include "ui/Themes.h"

#include <algorithm>

PersistGui captureGui(const App &app) {
  PersistGui g;
  g.outputIndex = static_cast<int>(app.outputTag);
  g.exportFormat = static_cast<int>(app.exportFormat);
  g.jpegQuality = app.jpegQuality;
  g.previewRes = static_cast<int>(app.previewRes);
  g.themeIndex = app.themeIndex;
  g.showLeft = app.showLeft;
  g.showRight = app.showRight;
  g.leftW = app.leftW;
  g.rightW = app.rightW;
  g.showFilmstrip = app.showFilmstrip;
  g.filmstripH = app.filmstripH;
  return g;
}

void applyGui(App &app, const PersistGui &g) {
  app.outputTag = outputSpace(g.outputIndex);
  app.exportFormat = static_cast<ExportFormat>(std::clamp(g.exportFormat, 0, 1));
  app.jpegQuality = std::clamp(g.jpegQuality, 1, 100);
  app.previewRes = static_cast<PreviewRes>(std::clamp(g.previewRes, 0, kPreviewResCount - 1));
  if (g.themeIndex >= 0 && g.themeIndex < themeCount()) app.themeIndex = g.themeIndex;
  app.showLeft = g.showLeft;
  app.showRight = g.showRight;
  app.leftW = g.leftW;
  app.rightW = g.rightW;
  app.showFilmstrip = g.showFilmstrip;
  app.filmstripH = std::clamp(g.filmstripH, 48.0f, 240.0f);
  app.themeApplyPending = true;
}

void saveCurrentInputSidecar(App &app) {
  if (app.path.empty()) return;
  saveInputSidecar(app.path, app.inputSpace, captureGui(app), captureChain(app));
}

void persistWorkspace(App &app) {
  if (app.workspaceDir.empty()) return;
  const std::string active =
      app.path.empty() ? std::string() : relativeToWorkspace(app.workspaceDir, app.path);
  saveWorkspaceProject(app.workspaceDir, captureGui(app), active);
}
