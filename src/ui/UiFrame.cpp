#include "ui/UiContext.h"

#include "DocumentActions.h"
#include "Themes.h"
#include "ui/DockLayout.h"

#include "imgui.h"
#include "portable-file-dialogs.h"

#include <GLFW/glfw3.h>

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
      if (ImGui::MenuItem("Open...", "Ctrl+O")) {
        auto f = pfd::open_file("Open image", "", {"Images", "*.*"});
        auto r = f.result();
        if (!r.empty()) openPath(app, r[0]);
      }
      if (ImGui::MenuItem("Open Workspace...")) {
        auto f = pfd::select_folder("Open workspace folder");
        auto r = f.result();
        if (!r.empty()) app.pendingWorkspaceDir = r;
      }
      if (ImGui::MenuItem("Save Project")) {
        saveCurrentInputSidecar(app);
        persistWorkspace(app);
        app.setStatus("Saved project and sidecar");
      }
      if (ImGui::MenuItem("Export...", "Ctrl+E")) doExport(app);
      if (ImGui::MenuItem("Quit", "Ctrl+Q")) glfwSetWindowShouldClose(app.window, 1);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Left panel", "Ctrl+[", &app.showLeft);
      ImGui::MenuItem("Right panel", "Ctrl+]", &app.showRight);
      ImGui::MenuItem("Filmstrip", "Ctrl+\\", &app.showFilmstrip);
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
}
