#include "persist/DocumentActions.h"

#include "NodeGraph.h"
#include "persist/ChainIO.h"
#include "ui/Themes.h"

#include <algorithm>

PersistGui captureGui(const App &app) {
  PersistGui g;
  g.outputIndex = static_cast<int>(app.gui.previewSpace);
  g.exportFormat = static_cast<int>(app.gui.exportFormat);
  g.exportBitDepth = app.gui.exportBitDepth;
  g.exportQuality = app.gui.exportQuality;
  g.exportLossless = app.gui.exportLossless;
  g.exportIndex = static_cast<int>(app.gui.exportSpace);
  g.exportIntent = static_cast<int>(app.gui.exportIntent);
  g.previewRes = static_cast<int>(app.gui.previewRes);
  g.themeIndex = app.gui.themeIndex;
  g.uiFontScale = static_cast<int>(app.gui.uiFontScale);
  g.showPlugins = app.gui.showPlugins;
  g.showParameters = app.gui.showParameters;
  g.showFilmstrip = app.gui.showFilmstrip;
  return g;
}

void applyGui(App &app, const PersistGui &g) {
  app.gui.previewSpace = outputSpace(g.outputIndex);
  app.gui.exportFormat = static_cast<ExportFormat>(std::clamp(g.exportFormat, 0, kExportFormatCount - 1));
  app.gui.exportBitDepth = 8;
  if (g.exportBitDepth >= 16) app.gui.exportBitDepth = 16;
  app.gui.exportQuality = std::clamp(g.exportQuality, 1, 100);
  app.gui.exportLossless = g.exportLossless;
  app.gui.exportSpace = outputSpace(std::clamp(g.exportIndex, 0, kColorSpaceCount - 1));
  app.gui.exportIntent = static_cast<RenderIntent>(std::clamp(g.exportIntent, 0, kRenderIntentCount - 1));
  app.gui.previewRes = static_cast<PreviewRes>(std::clamp(g.previewRes, 0, kPreviewResCount - 1));
  if (g.themeIndex >= 0 && g.themeIndex < themeCount()) app.gui.themeIndex = g.themeIndex;
  // The settings modal offers three scales; a stale or hand-edited value snaps
  // back into that range.
  app.gui.uiFontScale = static_cast<UiFontScale>(std::clamp(g.uiFontScale, 0, kUiFontScaleCount - 1));
  app.gui.showPlugins = g.showPlugins;
  app.gui.showParameters = g.showParameters;
  app.gui.showFilmstrip = g.showFilmstrip;
  app.gui.themeApplyPending = true;
  app.gui.fontApplyPending = true;
}

void saveCurrentInputSidecar(App &app) {
  if (app.doc.path.empty()) return;
  saveInputSidecar(app.doc.path, app.doc.fileSpace, app.doc.inputSpace, captureGui(app), captureChain(app));
}

void persistWorkspace(App &app) {
  // The per-user preferences file always reflects the current session, even when
  // no workspace is open, so the next start restores the theme, font, and view.
  saveGlobalSettings(captureGui(app));
  if (app.doc.workspaceDir.empty()) return;
  const std::string active =
      app.doc.path.empty() ? std::string() : relativeToWorkspace(app.doc.workspaceDir, app.doc.path);
  saveWorkspaceProject(app.doc.workspaceDir, captureGui(app), active);
}
