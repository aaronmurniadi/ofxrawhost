#include "ui/UiFrame.h"

#include "Actions.h"
#include "persist/DocumentActions.h"
#include "persist/ProjectPersist.h"
#include "ui/Themes.h"
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
#include <string>

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
      ImGui::MenuItem("Left panel", "⌘+[", &app.gui.showLeft);
      ImGui::MenuItem("Right panel", "⌘+]", &app.gui.showRight);
      ImGui::MenuItem("Filmstrip", "⌘+\\", &app.gui.showFilmstrip);
#else
      ImGui::MenuItem("Left panel", "Ctrl+[", &app.gui.showLeft);
      ImGui::MenuItem("Right panel", "Ctrl+]", &app.gui.showRight);
      ImGui::MenuItem("Filmstrip", "Ctrl+\\", &app.gui.showFilmstrip);
#endif
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
      float fontPt = app.gui.uiFontSizePt;
      ImGui::SetNextItemWidth(140.0f);
      if (ImGui::SliderFloat("UI font size", &fontPt, 10.0f, 22.0f, "%.0f pt")) {
        fontPt = std::round(fontPt);
        if (fontPt != app.gui.uiFontSizePt) {
          app.gui.uiFontSizePt = fontPt;
          ImGuiBackend_SetUIFontSize(app.window, fontPt);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Theme")) {
      for (int i = 0; i < themeCount(); ++i) {
        if (ImGui::MenuItem(themeName(i), nullptr, app.gui.themeIndex == i)) {
          app.gui.themeIndex = i;
          applyTheme(i);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
        app.gui.showAbout = true;
      }
      if (ImGui::MenuItem("Donate")) {
        app.gui.showDonate = true;
      }
      ImGui::EndMenu();
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
  if (ImGui::Button("Export…", ImVec2(120, 0))) {
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
    app.gui.showLeft = !app.gui.showLeft;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_RightBracket))
    app.gui.showRight = !app.gui.showRight;
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

  if (app.gui.showLeft) {
    if (ImGui::Begin(DockLayout::kLeft, &app.gui.showLeft)) drawLeftPanel(app);
    ImGui::End();
  }
  if (app.gui.showRight) {
    if (ImGui::Begin(DockLayout::kParams, &app.gui.showRight)) drawRightPanel(app);
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

  if (app.gui.showAbout) {
    ImGui::OpenPopup("About");
    app.gui.showAbout = false;
  }
  if (ImGui::BeginPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("OfxRawHost");
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

  drawExportDialog(app);
}
