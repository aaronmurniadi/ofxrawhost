#include "ui/UiContext.h"

#include "persist/DocumentActions.h"
#include "persist/ProjectPersist.h"
#include "ui/Themes.h"
#include "ui/DockLayout.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <GLFW/glfw3.h>

#include <cstdlib>

static void openUrl(const std::string &url) {
#if defined(_WIN32)
  std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  std::string cmd = "open \"" + url + "\"";
#else
  std::string cmd = "xdg-open \"" + url + "\"";
#endif
  std::system(cmd.c_str());
}

void DrawUiFrame(App &app) {
  if (app.themeApplyPending) {
    applyTheme(app.themeIndex);
    app.themeApplyPending = false;
  }
  if (!app.pendingWorkspaceDir.empty()) {
    const std::string dir = std::move(app.pendingWorkspaceDir);
    app.pendingWorkspaceDir.clear();
    openWorkspace(app, dir);
  }

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
        if (!r.empty()) app.pendingWorkspaceDir = r;
      }
      if (ImGui::MenuItem("Save Project")) {
        saveCurrentInputSidecar(app);
        persistWorkspace(app);
        app.setStatus("Saved project and sidecar");
      }
#ifdef __APPLE__
      if (ImGui::MenuItem("Export", "⌘+E")) doExport(app);
#else
      if (ImGui::MenuItem("Export", "Ctrl+E")) doExport(app);
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
      ImGui::MenuItem("Left panel", "⌘+[", &app.showLeft);
      ImGui::MenuItem("Right panel", "⌘+]", &app.showRight);
      ImGui::MenuItem("Filmstrip", "⌘+\\", &app.showFilmstrip);
#else
      ImGui::MenuItem("Left panel", "Ctrl+[", &app.showLeft);
      ImGui::MenuItem("Right panel", "Ctrl+]", &app.showRight);
      ImGui::MenuItem("Filmstrip", "Ctrl+\\", &app.showFilmstrip);
#endif
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Theme")) {
      for (int i = 0; i < themeCount(); ++i) {
        if (ImGui::MenuItem(themeName(i), nullptr, app.themeIndex == i)) {
          app.themeIndex = i;
          applyTheme(i);
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About")) {
        app.showAbout = true;
      }
      if (ImGui::MenuItem("Donate")) {
        app.showDonate = true;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_LeftBracket) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_LeftBracket))
    app.showLeft = !app.showLeft;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_RightBracket))
    app.showRight = !app.showRight;
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Backslash) ||
      ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_Backslash))
    app.showFilmstrip = !app.showFilmstrip;
#ifdef __APPLE__
  if (ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiMod_Shift | ImGuiKey_O)) {
#else
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O)) {
#endif
    auto f = pfd::select_folder("Open workspace folder");
    auto r = f.result();
    if (!r.empty()) app.pendingWorkspaceDir = r;
  }

  DockLayout::BeginMainDockSpace(app);

  if (app.showLeft) {
    if (ImGui::Begin(DockLayout::kLeft, &app.showLeft)) drawLeftPanel(app);
    ImGui::End();
  }
  if (app.showRight) {
    if (ImGui::Begin(DockLayout::kParams, &app.showRight)) drawRightPanel(app);
    ImGui::End();
  }
  {
    ImGuiWindowFlags previewFlags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(DockLayout::kPreview, nullptr, previewFlags)) drawPreviewPanel(app);
    ImGui::End();
  }
  if (app.showFilmstrip) {
    if (ImGui::Begin(DockLayout::kFilmstrip, &app.showFilmstrip)) drawFilmstripPanel(app);
    ImGui::End();
  }

  if (app.showAbout) {
    ImGui::OpenPopup("About");
    app.showAbout = false;
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

  if (app.showDonate) {
    ImGui::OpenPopup("Donate");
    app.showDonate = false;
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
}
