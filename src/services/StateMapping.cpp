#include "services/StateMapping.h"

#include "core/UpdateChannel.h"
#include "model/GuiState.h"

#include <algorithm>

PersistGui captureGui(const GuiState &gui) {
  PersistGui g;
  g.outputIndex = static_cast<int>(gui.previewSpace);
  g.exportFormat = static_cast<int>(gui.exportFormat);
  g.exportBitDepth = gui.exportBitDepth;
  g.exportQuality = gui.exportQuality;
  g.exportLossless = gui.exportLossless;
  g.exportSidecar = gui.exportSidecar;
  g.exportIndex = static_cast<int>(gui.exportSpace);
  g.exportIntent = static_cast<int>(gui.exportIntent);
  g.previewRes = static_cast<int>(gui.previewRes);
  g.showPlugins = gui.showPlugins;
  g.showParameters = gui.showParameters;
  g.showFilmstrip = gui.showFilmstrip;
  return g;
}

void applyGui(GuiState &gui, const PersistGui &g) {
  gui.previewSpace = outputSpace(g.outputIndex);
  gui.exportFormat = static_cast<ExportFormat>(std::clamp(g.exportFormat, 0, kExportFormatCount - 1));
  gui.exportBitDepth = 8;
  if (g.exportBitDepth >= 16) gui.exportBitDepth = 16;
  gui.exportQuality = std::clamp(g.exportQuality, 1, 100);
  gui.exportLossless = g.exportLossless;
  gui.exportSidecar = g.exportSidecar;
  gui.exportSpace = outputSpace(std::clamp(g.exportIndex, 0, kColorSpaceCount - 1));
  gui.exportIntent = static_cast<RenderIntent>(std::clamp(g.exportIntent, 0, kRenderIntentCount - 1));
  gui.previewRes = static_cast<PreviewRes>(std::clamp(g.previewRes, 0, kPreviewResCount - 1));
  gui.showPlugins = g.showPlugins;
  gui.showParameters = g.showParameters;
  gui.showFilmstrip = g.showFilmstrip;
}

PersistAppearance captureAppearance(const GuiState &gui) {
  PersistAppearance a;
  a.themeIndex = gui.themeIndex;
  a.uiFontPt = gui.uiFontPt;
  return a;
}

void applyAppearance(GuiState &gui, const PersistAppearance &appearance) {
  if (appearance.themeIndex >= 0 && appearance.themeIndex < kThemeCount)
    gui.themeIndex = appearance.themeIndex;
  // The settings modal offers a point size; a stale or hand-edited value snaps
  // back into that range, and 0 stays on the system size.
  if (appearance.uiFontPt <= 0) {
    gui.uiFontPt = kUiFontPtAuto;
  } else {
    gui.uiFontPt = std::clamp(appearance.uiFontPt, kUiFontPtMin, kUiFontPtMax);
  }
  gui.themeApplyPending = true;
  gui.fontApplyPending = true;
}

PersistUpdates captureUpdates(const GuiState &gui) {
  PersistUpdates u;
  u.channel = static_cast<int>(gui.updateChannel);
  u.checkAtStartup = gui.checkUpdatesAtStartup;
  u.devWarningShown = gui.devBuildWarningShown;
  return u;
}

void applyUpdates(GuiState &gui, const PersistUpdates &u) {
  // A release build checks releases only, so a stored pre-release channel snaps
  // back to stable instead of leaving the setting quietly in effect.
  UpdateChannel channel = static_cast<UpdateChannel>(u.channel);
  if (u.channel < 0 || u.channel >= kUpdateChannelCount) channel = defaultUpdateChannel();
  if (!channelAllowed(channel)) channel = UpdateChannel::Stable;
  gui.updateChannel = channel;
  gui.checkUpdatesAtStartup = u.checkAtStartup;
  gui.devBuildWarningShown = u.devWarningShown;
}

