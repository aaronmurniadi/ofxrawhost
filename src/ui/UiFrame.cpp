#include "ui/UiFrame.h"

#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"
#include "services/managers/session/ColorSpaceManager.h"
#include "services/NodeGraph.h"
#include "ofx/OfxHost.h"
#include "persist/ProjectPersist.h"
#include "platform/SystemUiFont.h"
#include "engine/RenderWorker.h"
#include "services/UpdateCheck.h"
#include "ui/Themes.h"
#include "services/ParamEdit.h"
#include "ui/DockLayout.h"
#include "ui/ImGuiBackend.h"
#include "ui/Widgets.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

// The build passes the release version in. Keep a fallback for builds that do
// not, so the About window always shows a version.
#ifndef OFXRAWHOST_VERSION
#define OFXRAWHOST_VERSION "0.0.0"
#endif
#include <string>
#include <vector>

static void openUrl(const std::string &url) {
#if defined(_WIN32)
  // ShellExecute opens the default browser without a console window, which
  // system("start ...") would create for a GUI build.
  const std::wstring wide(url.begin(), url.end());
  ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  const std::string cmd = "open \"" + url + "\"";
  std::system(cmd.c_str());
#else
  const std::string cmd = "xdg-open \"" + url + "\"";
  std::system(cmd.c_str());
#endif
}

static void drawMenuBar(App &app) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
#ifdef __APPLE__
      if (ImGui::MenuItem("Open image", "⌘+O")) {
#else
      if (ImGui::MenuItem("Open image", "Ctrl+O")) {
#endif
        auto f = pfd::open_file("Open image", "", openImageDialogFilters());
        auto r = f.result();
        if (!r.empty()) app.documents->openPath(r[0]);
      }
#ifdef __APPLE__
      if (ImGui::MenuItem("Open Directory", "⌘+Shift+O")) {
#else
      if (ImGui::MenuItem("Open Directory", "Ctrl+Shift+O")) {
#endif
        auto f = pfd::select_folder("Open directory");
        auto r = f.result();
        if (!r.empty()) app.gui.pendingWorkspaceDir = r;
      }
      if (ImGui::MenuItem("Save Project")) {
        app.documents->saveCurrentInputSidecar();
        app.documents->persistWorkspace();
        app.reportStatus("Saved project and sidecar");
      }
#ifdef __APPLE__
      if (ImGui::MenuItem("Export", "⌘+E")) exportImage(app);
#else
      if (ImGui::MenuItem("Export", "Ctrl+E")) exportImage(app);
#endif
#ifdef __APPLE__
      if (ImGui::MenuItem("Quit", "⌘+Q")) glfwSetWindowShouldClose(app.window, 1);
#else
      if (ImGui::MenuItem("Quit", "Ctrl+Q")) glfwSetWindowShouldClose(app.window, 1);
#endif
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
#ifdef __APPLE__
      ImGui::MenuItem("Plugins", "⌘+[", &app.gui.showPlugins);
      ImGui::MenuItem("Parameters", "⌘+]", &app.gui.showParameters);
      ImGui::MenuItem("Filmstrip", "⌘+\\", &app.gui.showFilmstrip);
#else
      ImGui::MenuItem("Plugins", "Ctrl+[", &app.gui.showPlugins);
      ImGui::MenuItem("Parameters", "Ctrl+]", &app.gui.showParameters);
      ImGui::MenuItem("Filmstrip", "Ctrl+\\", &app.gui.showFilmstrip);
#endif
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Settings")) app.gui.showSettings = true;
    if (ImGui::BeginMenu("Theme")) {
      for (int i = 0; i < themeCount(); ++i) {
        if (ImGui::MenuItem(themeName(i), nullptr, app.gui.themeIndex == i)) {
          app.gui.themeIndex = i;
          applyTheme(i);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Donate")) {
      app.gui.showDonate = true;
    }
    if (ImGui::MenuItem("About")) {
      app.gui.showAbout = true;
    }
    ImGui::EndMainMenuBar();
  }
}

// Save-dialog filters, one pair per ExportFormat value.
static const char *const kExportFilters[] = {
    "PNG (8/16-bit)", "*.png",
    "JPEG",            "*.jpg *.jpeg",
    "TIFF (8/16-bit)", "*.tif *.tiff",
    "WebP",            "*.webp",
    "JPEG XL",         "*.jxl",
};

// Opens the export dialog; the destination is chosen from inside it.
void exportImage(App &app) {
  if (!app.exports->canExport()) return;
  app.gui.showExportDialog = true;
}

static void drawExportDialog(App &app) {
  if (app.gui.showExportDialog) {
    ImGui::OpenPopup("Export");
    app.gui.showExportDialog = false;
  }
  if (!ImGui::BeginPopupModal("Export", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  static const char *const kFormats[kExportFormatCount] = {"PNG", "JPEG", "TIFF", "WebP", "JPEG XL"};
  int fmt = static_cast<int>(app.gui.exportFormat);
  if (uiChoiceCombo("Format", nullptr, kFormats, kExportFormatCount, &fmt))
    app.gui.exportFormat = static_cast<ExportFormat>(fmt);

  // The color space the exported file is written in. The chain writes the preview
  // space, so the export converts into this one with the chosen intent.
  static const char *const kIntents[kRenderIntentCount] = {"Perceptual", "Relative colorimetric", "Saturation",
                                                          "Absolute colorimetric"};
  int outSpace = static_cast<int>(app.gui.exportSpace);
  if (uiChoiceCombo("Export colorspace", nullptr, kColorSpaces, kColorSpaceCount, &outSpace))
    app.colorSpaces->setExportSpace(outSpace);
  int intent = static_cast<int>(app.gui.exportIntent);
  if (uiChoiceCombo("Intent", nullptr, kIntents, kRenderIntentCount, &intent))
    app.colorSpaces->setExportIntent(intent);

  const ExportFormat format = app.gui.exportFormat;
  if (exportUsesBitDepth(format)) {
    static const char *const kDepths[] = {"8-bit", "16-bit"};
    int depth = 0;
    if (app.gui.exportBitDepth >= 16) depth = 1;
    if (uiChoiceCombo("Bit depth", nullptr, kDepths, 2, &depth)) {
      app.gui.exportBitDepth = 8;
      if (depth == 1) app.gui.exportBitDepth = 16;
    }
  }

  bool lossless = false;
  if (exportSupportsLossless(format)) {
    ImGui::Checkbox("Lossless", &app.gui.exportLossless);
    lossless = app.gui.exportLossless;
  }
  if (exportUsesQuality(format) && !lossless) ImGui::SliderInt("Quality", &app.gui.exportQuality, 1, 100);

  ImGui::Spacing();
  ImGui::Checkbox("Export sidecar", &app.gui.exportSidecar);
  ImGui::TextDisabled("Records the chain that made this file, in the application state directory.");

  ImGui::Separator();
  if (ImGui::Button("Export", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
    const int idx = static_cast<int>(format);
    auto sel = pfd::save_file("Export", app.exports->defaultExportName(),
                             {kExportFilters[idx * 2], kExportFilters[idx * 2 + 1]});
    const std::string outPath = sel.result();
    if (!outPath.empty()) app.exports->doExport(outPath);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// One row per section of the settings sidebar. The index is the value kept in
// GuiState::settingsSection.
static const char *const kSettingsSections[] = {"Font", "Updates"};
static constexpr int kSettingsSectionCount = 2;

// The settings modal sets the size of the bundled Inter font in points. Zero,
// which is the default, follows the operating system font size, so a new install
// looks native; the step buttons set an explicit size instead.

// The system font size as a whole number of points, held inside the range the
// step buttons offer.
static int systemFontPt() {
  const float pt = systemUiFontSizePt();
  int rounded = 13;
  if (std::isfinite(pt)) rounded = (int)std::lround(pt);
  return std::clamp(rounded, kUiFontPtMin, kUiFontPtMax);
}

// A font change rebuilds the ImGui font atlas, which frees the font objects the
// running frame still points at. Raise a flag instead, and let the main loop
// rebuild the atlas between frames.
static void requestUiFont(App &app) {
  app.gui.fontApplyPending = true;
}

// Sets the interface font size and returns true when the value changed. The
// main loop reloads the font once per frame, so a second change in the same
// frame costs the same as the first.
static bool setUiFontPt(App &app, int pt) {
  if (pt < kUiFontPtMin || pt > kUiFontPtMax || pt == app.gui.uiFontPt) return false;
  app.gui.uiFontPt = pt;
  return true;
}

static void drawFontsSettings(App &app) {
  // The UI font is bundled (Inter), so only its size is configurable. This
  // keeps the interface identical on every platform.
  ImGui::TextUnformatted("Font");
  ImGui::Separator();
  ImGui::TextUnformatted("Inter");
  ImGui::TextDisabled("The interface font is bundled and cannot be changed.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Font size");
  const int systemPt = systemFontPt();
  bool followSystem = app.gui.uiFontPt <= kUiFontPtAuto;
  char followLabel[64];
  std::snprintf(followLabel, sizeof(followLabel), "Follow the system font size (%d pt)", systemPt);
  if (ImGui::Checkbox(followLabel, &followSystem)) {
    if (followSystem) app.gui.uiFontPt = kUiFontPtAuto;
    else app.gui.uiFontPt = systemPt;
    requestUiFont(app);
  }
  ImGui::TextDisabled("The system size is what the operating system uses for its own interface text.");

  // The field shows the size that is in effect, which is the system size while
  // the box above is checked. The buttons are inert in that state.
  int shownPt = app.gui.uiFontPt;
  if (followSystem) shownPt = systemPt;
  if (followSystem) ImGui::BeginDisabled();
  bool changed = false;
  if (ImGui::Button("-", ImVec2(32.0f, 0.0f))) changed = setUiFontPt(app, shownPt - 1);
  ImGui::SameLine();
  ImGui::Text("%d pt", shownPt);
  ImGui::SameLine();
  if (ImGui::Button("+", ImVec2(32.0f, 0.0f))) changed = setUiFontPt(app, shownPt + 1);
  if (followSystem) ImGui::EndDisabled();
  if (changed) requestUiFont(app);
  ImGui::TextDisabled("Points, between %d and %d. Clear the box to set a size of your own.", kUiFontPtMin,
                      kUiFontPtMax);
}

// A version as the user reads it: the About window shows the build version
// without the "v" of the release tag, and the update lines match it.
static std::string displayVersion(const std::string &tag) {
  if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) return tag.substr(1);
  return tag;
}

// The update section: what the check does, and the answer of the last one. Every
// line names the channel and the version, so a result that was measured against
// the other channel is never read as the current state.
static void drawUpdatesSettings(App &app) {
  ImGui::TextUnformatted("Updates");
  ImGui::Separator();
  ImGui::Text("Version %s", hostVersion().c_str());
  if (isDevelopmentBuild()) ImGui::TextDisabled("This is a development build, so it changes often.");

  ImGui::Spacing();
  static const char *const kChannelHints[kUpdateChannelCount] = {
      "Only full releases. A development build may be newer than the newest release.",
      "Pre-release builds as well, which is where development builds are published.",
  };
  int channel = static_cast<int>(app.gui.updateChannel);
  if (channel < 0 || channel >= kUpdateChannelCount) channel = static_cast<int>(defaultUpdateChannel());
  // A release build checks releases only, so it cannot pick the other entry.
  bool channelEnabled[kUpdateChannelCount];
  for (int i = 0; i < kUpdateChannelCount; ++i) channelEnabled[i] = channelAllowed(static_cast<UpdateChannel>(i));
  if (uiChoiceCombo("Update Channel", nullptr, kUpdateChannelLabels, kUpdateChannelCount, &channel, channelEnabled))
    app.gui.updateChannel = static_cast<UpdateChannel>(channel);
  ImGui::TextDisabled("%s", kChannelHints[static_cast<int>(app.gui.updateChannel)]);
  if (!isDevelopmentBuild()) ImGui::TextDisabled("Only a development build can follow pre-releases.");

  ImGui::Spacing();
  ImGui::Checkbox("Check for updates at startup", &app.gui.checkUpdatesAtStartup);
  ImGui::TextDisabled("The check asks GitHub for the release list. Nothing about your images is sent.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Status");
  const bool preReleases = app.updates.checkedChannel == UpdateChannel::Prereleases;
  switch (app.updates.state) {
    case UpdateCheck::State::Idle:
      ImGui::TextDisabled("No check has run yet.");
      break;
    case UpdateCheck::State::Checking:
      ImGui::TextUnformatted("Checking for updates...");
      break;
    case UpdateCheck::State::UpToDate:
      if (preReleases) ImGui::TextUnformatted("You are running the newest version.");
      else ImGui::TextUnformatted("You are running the newest release.");
      break;
    case UpdateCheck::State::Available:
      ImGui::Text("Version %s is available.", displayVersion(app.updates.latestTag).c_str());
      break;
    case UpdateCheck::State::Failed:
      ImGui::TextDisabled("The check did not finish: %s", app.updates.failure.c_str());
      break;
  }
  // Two development builds can share one version number, and their commit hashes
  // cannot be ordered, so the newest published tag is worth naming even when the
  // check calls this build current.
  if (app.updates.state == UpdateCheck::State::UpToDate && !app.updates.latestTag.empty() &&
      !sameVersionText(app.updates.latestTag, hostVersion()))
    ImGui::TextDisabled("Newest published: %s", displayVersion(app.updates.latestTag).c_str());

  ImGui::Spacing();
  const bool checking = app.updates.state == UpdateCheck::State::Checking;
  if (checking) ImGui::BeginDisabled();
  if (ImGui::Button("Check now")) app.updates.startCheck(hostVersion(), app.gui.updateChannel);
  if (checking) ImGui::EndDisabled();
  if (app.updates.state == UpdateCheck::State::Available && !app.updates.releaseUrl.empty()) {
    ImGui::SameLine();
    if (ImGui::Button("Open release page")) openUrl(app.updates.releaseUrl);
  }
}

// One-time notice on a development build, which publishes often enough that a
// start-up check is a choice rather than a given. The marker is recorded when the
// window opens, so it appears once even if it is dismissed with Escape.
static void drawDevBuildWarning(App &app) {
  if (app.gui.showDevBuildWarning) {
    ImGui::OpenPopup("Development build");
    app.gui.showDevBuildWarning = false;
    app.gui.devBuildWarningShown = true;
    app.documents->persistWorkspace();
  }
  if (!ImGui::BeginPopupModal("Development build", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  ImGui::TextWrapped(
      "This is a development build, and it is updated often. Would you like to turn off checking for updates at "
      "startup?");
  ImGui::TextDisabled("You can turn it back on at any time under Settings, Updates.");
  ImGui::Spacing();
  if (ImGui::Button("Yes, turn it off", ImVec2(160.0f, 0.0f))) {
    app.gui.checkUpdatesAtStartup = false;
    app.documents->persistWorkspace();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("No, keep checking", ImVec2(160.0f, 0.0f))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// A modal with a section sidebar on the left and the controls of the selected
// section on the right.
static void drawSettingsModal(App &app) {
  if (app.gui.showSettings) {
    ImGui::OpenPopup("Settings");
    app.gui.showSettings = false;
  }
  ImGui::SetNextWindowSize(ImVec2(720.0f, 460.0f), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Settings", nullptr)) return;

  // The footer holds the Close button, so both children stop above it.
  const float footer = ImGui::GetFrameHeightWithSpacing();
  // The sidebar follows the modal width instead of a fixed 170 px, so a long
  // section name has room and the panel uses the space when the modal grows.
  float sidebarW = ImGui::GetContentRegionAvail().x * 0.25f;
  if (sidebarW < 140.0f) sidebarW = 140.0f;
  if (sidebarW > 260.0f) sidebarW = 260.0f;
  if (ImGui::BeginChild("settings_sidebar", ImVec2(sidebarW, -footer), ImGuiChildFlags_Borders)) {
    for (int i = 0; i < kSettingsSectionCount; ++i) {
      bool selected = app.gui.settingsSection == i;
      if (ImGui::Selectable(kSettingsSections[i], selected)) app.gui.settingsSection = i;
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();

  if (ImGui::BeginChild("settings_panel", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders)) {
    if (app.gui.settingsSection == 0) drawFontsSettings(app);
    if (app.gui.settingsSection == 1) drawUpdatesSettings(app);
  }
  ImGui::EndChild();

  if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// "Fit to preview" follows the preview panel size. Wait for the size to settle,
// so dragging a dock splitter does not queue a rebuild on every frame.
static void applyFitPreviewSize(App &app) {
  if (app.gui.previewRes != PreviewRes::Fit || app.document.full.px.empty()) return;
  if (previewMaxEdge(app.gui) == app.render.builtPreviewMaxEdge()) {
    app.gui.previewFitSettleTime = 0.0;
    return;
  }
  const double now = ImGui::GetTime();
  if (app.gui.previewFitSettleTime == 0.0) {
    app.gui.previewFitSettleTime = now;
    return;
  }
  if (now - app.gui.previewFitSettleTime < 0.25) return;
  app.gui.previewFitSettleTime = 0.0;
  app.rebuildPreviewImage();
}

// Confirms a parameter reset, because one click discards the whole edit of the
// selected plugin.
static void drawResetParamsConfirm(App &app) {
  if (app.gui.showResetParamsConfirm) {
    ImGui::OpenPopup("Reset Parameters");
    app.gui.showResetParamsConfirm = false;
  }
  if (!ImGui::BeginPopupModal("Reset Parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  Node *node = selectedNode(app.session());
  std::string label;
  if (node) label = plugins()[node->pluginIndex].label;
  ImGui::TextWrapped("Return every parameter of %s to its default value?", label.c_str());
  ImGui::TextDisabled("This cannot be undone.");
  ImGui::Spacing();
  if (ImGui::Button("Reset", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
    if (node) {
      const int count = resetNodeParamsToDefaults(app.session(), *node);
      app.reportStatus("Reset " + std::to_string(count) + " parameters of " + label);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// The chain saved for the image that is opening and the chain in place can both
// hold work, so the open waits here for the user to pick one.
static void drawSidecarPrompt(App &app) {
  if (app.gui.showSidecarPrompt) {
    ImGui::OpenPopup("Saved parameters");
    app.gui.showSidecarPrompt = false;
  }
  if (!ImGui::BeginPopupModal("Saved parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  ImGui::TextWrapped(
      "This image has parameters saved for it. Open it with those parameters? The parameters of the image that is "
      "open now will be replaced.");
  if (!app.gui.pendingOpenPath.empty())
    ImGui::TextDisabled("%s", std::filesystem::path(app.gui.pendingOpenPath).filename().string().c_str());
  ImGui::TextDisabled(
      "Yes restores the parameters saved for this image. No keeps the parameters of the image that is open now and "
      "applies them to this one.");
  ImGui::Spacing();
  if (ImGui::Button("Yes", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
    app.documents->resolvePendingOpen(/*useSavedChain=*/true);
  }
  ImGui::SameLine();
  if (ImGui::Button("No", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
    app.documents->resolvePendingOpen(/*useSavedChain=*/false);
  }
  ImGui::EndPopup();
}

void drawUiFrame(App &app) {
  if (app.gui.themeApplyPending) {
    applyTheme(app.gui.themeIndex);
    app.gui.themeApplyPending = false;
  }
  if (!app.gui.pendingWorkspaceDir.empty()) {
    const std::string dir = std::move(app.gui.pendingWorkspaceDir);
    app.gui.pendingWorkspaceDir.clear();
    app.documents->openWorkspace(dir);
  }

  drawMenuBar(app);

  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_LeftBracket) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_LeftBracket))
    app.gui.showPlugins = !app.gui.showPlugins;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_RightBracket))
    app.gui.showParameters = !app.gui.showParameters;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Backslash) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_Backslash))
    app.gui.showFilmstrip = !app.gui.showFilmstrip;
#ifdef __APPLE__
  if (ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiMod_Shift | ImGuiKey_O)) {
#else
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O)) {
#endif
    auto f = pfd::select_folder("Open directory");
    auto r = f.result();
    if (!r.empty()) app.gui.pendingWorkspaceDir = r;
  }

  DockLayout::BeginMainDockSpace();

  if (app.gui.showPlugins) {
    if (ImGui::Begin(DockLayout::kPlugins, &app.gui.showPlugins)) drawPluginsPanel(app);
    ImGui::End();
  }
  if (app.gui.showParameters) {
    if (ImGui::Begin(DockLayout::kParameters, &app.gui.showParameters)) drawParametersPanel(app);
    ImGui::End();
  }
  {
    static ImGuiWindowClass previewDockClass;
    static bool previewDockClassInit = false;
    if (!previewDockClassInit) {
      previewDockClass.ClassId = ImGui::GetID("PreviewPanelDock");
      previewDockClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
      previewDockClassInit = true;
    }
    ImGui::SetNextWindowClass(&previewDockClass);
    ImGuiWindowFlags previewFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(DockLayout::kPreview, nullptr, previewFlags)) drawPreviewPanel(app);
    ImGui::End();
  }
  if (app.gui.showFilmstrip) {
    if (ImGui::Begin(DockLayout::kFilmstrip, &app.gui.showFilmstrip)) drawFilmstripPanel(app);
    ImGui::End();
  }

  applyFitPreviewSize(app);

  if (app.gui.showAbout) {
    ImGui::OpenPopup("About");
    app.gui.showAbout = false;
  }
  if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("OfxRawHost");
    ImGui::SameLine();
    ImGui::TextDisabled("Version " OFXRAWHOST_VERSION);
    ImGui::Separator();
    ImGui::TextWrapped("A free and open-source OFX raw image host for color grading and plugin-based processing.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Repository:");
    if (ImGui::Button("github.com/aaronmurniadi/ofxrawhost")) {
      openUrl("https://github.com/aaronmurniadi/ofxrawhost");
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (app.gui.showDonate) {
    ImGui::OpenPopup("Donate");
    app.gui.showDonate = false;
  }
  if (ImGui::BeginPopupModal("Donate", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("OfxRawHost is free and open-source. If you find it useful, consider supporting its development.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Buy me a coffee:");
    if (ImGui::Button("buymeacoffee.com/aaronmurniadi")) {
      openUrl("https://buymeacoffee.com/aaronmurniadi");
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  drawResetParamsConfirm(app);
  drawSidecarPrompt(app);
  drawExportDialog(app);
  drawSettingsModal(app);
  drawDevBuildWarning(app);
}
