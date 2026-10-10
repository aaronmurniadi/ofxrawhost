#include "ui/UiFrame.h"

#include "Actions.h"
#include "NodeGraph.h"
#include "ofx/OfxHost.h"
#include "persist/DocumentActions.h"
#include "persist/ProjectPersist.h"
#include "RenderScheduler.h"
#include "ui/Themes.h"
#include "ui/ParamEdit.h"
#include "ui/DockLayout.h"
#include "ui/ImGuiBackend.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cmath>
#include <cstdlib>

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
        if (!r.empty()) openPath(app, r[0]);
      }
#ifdef __APPLE__
      if (ImGui::MenuItem("Open Workspace", "⌘+Shift+O")) {
#else
      if (ImGui::MenuItem("Open Workspace", "Ctrl+Shift+O")) {
#endif
        auto f = pfd::select_folder("Open workspace folder");
        auto r = f.result();
        if (!r.empty()) app.gui.pendingWorkspaceDir = r;
      }
      if (ImGui::MenuItem("Save Project")) {
        saveCurrentInputSidecar(app);
        persistWorkspace(app);
        app.setStatus("Saved project and sidecar");
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
  if (!canExport(app)) return;
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
  if (ImGui::Combo("Format", &fmt, kFormats, kExportFormatCount)) app.gui.exportFormat = static_cast<ExportFormat>(fmt);

  // The color space the exported file is written in. The chain writes the preview
  // space, so the export converts into this one with the chosen intent.
  static const char *const kIntents[kRenderIntentCount] = {"Perceptual", "Relative colorimetric", "Saturation",
                                                          "Absolute colorimetric"};
  int outSpace = static_cast<int>(app.gui.exportSpace);
  if (ImGui::Combo("Export colorspace", &outSpace, kColorSpaces, kColorSpaceCount))
    setExportSpace(app, outSpace);
  int intent = static_cast<int>(app.gui.exportIntent);
  if (ImGui::Combo("Intent", &intent, kIntents, kRenderIntentCount)) setExportIntent(app, intent);

  const ExportFormat format = app.gui.exportFormat;
  if (exportUsesBitDepth(format)) {
    int depth = 0;
    if (app.gui.exportBitDepth >= 16) depth = 1;
    if (ImGui::Combo("Bit depth", &depth, "8-bit\0" "16-bit\0")) {
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

  ImGui::Separator();
  if (ImGui::Button("Export", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
    const int idx = static_cast<int>(format);
    auto sel = pfd::save_file("Export", defaultExportName(app), {kExportFilters[idx * 2], kExportFilters[idx * 2 + 1]});
    const std::string outPath = sel.result();
    if (!outPath.empty()) doExport(app, outPath);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// One row per section of the settings sidebar. The index is the value kept in
// GuiState::settingsSection.
static const char *const kSettingsSections[] = {"Font"};
static constexpr int kSettingsSectionCount = 1;

// The settings modal offers three scales of the bundled Inter font. Normal is
// the operating system font size, so a new install looks native.
static const char *const kFontScaleLabels[kUiFontScaleCount] = {"Smaller", "Normal", "Bigger"};

// A font change rebuilds the ImGui font atlas, which frees the font objects the
// running frame still points at. Raise a flag instead, and let the main loop
// rebuild the atlas between frames.
static void requestUiFont(App &app) {
  app.gui.fontApplyPending = true;
}

static void drawFontsSettings(App &app) {
  // The UI font is bundled (Inter), so only its scale is configurable. This
  // keeps the interface identical on every platform.
  ImGui::TextUnformatted("Font");
  ImGui::Separator();
  ImGui::TextUnformatted("Inter");
  ImGui::TextDisabled("The interface font is bundled and cannot be changed.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Font size");
  int scale = static_cast<int>(app.gui.uiFontScale);
  if (scale < 0 || scale >= kUiFontScaleCount) scale = static_cast<int>(UiFontScale::Normal);
  for (int i = 0; i < kUiFontScaleCount; ++i) {
    if (ImGui::RadioButton(kFontScaleLabels[i], scale == i)) {
      app.gui.uiFontScale = static_cast<UiFontScale>(i);
      requestUiFont(app);
    }
  }
  ImGui::TextDisabled("Normal follows the system font size.");
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
  }
  ImGui::EndChild();

  if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

// "Fit to preview" follows the preview panel size. Wait for the size to settle,
// so dragging a dock splitter does not queue a rebuild on every frame.
static void applyFitPreviewSize(App &app) {
  if (app.gui.previewRes != PreviewRes::Fit || app.doc.full.px.empty()) return;
  if (previewMaxEdge(app.gui) == app.render.builtPreviewMaxEdge) {
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
  rebuildPreview(app);
}

// Confirms a parameter reset, because one click discards the whole edit of the
// selected plugin.
static void drawResetParamsConfirm(App &app) {
  if (app.gui.showResetParamsConfirm) {
    ImGui::OpenPopup("Reset Parameters");
    app.gui.showResetParamsConfirm = false;
  }
  if (!ImGui::BeginPopupModal("Reset Parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  Node *node = selectedNode(app);
  std::string label;
  if (node) label = gPlugins[node->pluginIndex].label;
  ImGui::TextWrapped("Return every parameter of %s to its default value?", label.c_str());
  ImGui::TextDisabled("This cannot be undone.");
  ImGui::Spacing();
  if (ImGui::Button("Reset", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
    if (node) {
      const int count = resetNodeParamsToDefaults(app, *node);
      app.setStatus("Reset " + std::to_string(count) + " parameters of " + label);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
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
    if (openWorkspace(app, dir) && !ImGuiBackend_SetWorkspaceIni(app.doc.workspaceDir))
      app.gui.layoutApplyPending = true;
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
    auto f = pfd::select_folder("Open workspace folder");
    auto r = f.result();
    if (!r.empty()) app.gui.pendingWorkspaceDir = r;
  }

  DockLayout::BeginMainDockSpace(app);

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
  drawExportDialog(app);
  drawSettingsModal(app);
}
