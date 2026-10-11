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
  g.uiFontPt = app.gui.uiFontPt;
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
  // The settings modal offers a point size; a stale or hand-edited value snaps
  // back into that range, and 0 stays on the system size.
  if (g.uiFontPt <= 0) {
    app.gui.uiFontPt = kUiFontPtAuto;
  } else {
    app.gui.uiFontPt = std::clamp(g.uiFontPt, kUiFontPtMin, kUiFontPtMax);
  }
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

PersistUpdates captureUpdates(const App &app) {
  PersistUpdates u;
  u.channel = static_cast<int>(app.gui.updateChannel);
  u.checkAtStartup = app.gui.checkUpdatesAtStartup;
  u.devWarningShown = app.gui.devBuildWarningShown;
  return u;
}

void applyUpdates(App &app, const PersistUpdates &u) {
  // A release build checks releases only, so a stored pre-release channel snaps
  // back to stable instead of leaving the setting quietly in effect.
  UpdateChannel channel = static_cast<UpdateChannel>(u.channel);
  if (u.channel < 0 || u.channel >= kUpdateChannelCount) channel = defaultUpdateChannel();
  if (!channelAllowed(channel)) channel = UpdateChannel::Stable;
  app.gui.updateChannel = channel;
  app.gui.checkUpdatesAtStartup = u.checkAtStartup;
  app.gui.devBuildWarningShown = u.devWarningShown;
}

void persistWorkspace(App &app) {
  // The per-user preferences file always reflects the current session, even when
  // no workspace is open, so the next start restores the theme, font, and view.
  saveGlobalSettings(captureGui(app), captureUpdates(app));
  if (app.doc.workspaceDir.empty()) return;
  const std::string active =
      app.doc.path.empty() ? std::string() : relativeToWorkspace(app.doc.workspaceDir, app.doc.path);
  saveWorkspaceProject(app.doc.workspaceDir, captureGui(app), active);
}
