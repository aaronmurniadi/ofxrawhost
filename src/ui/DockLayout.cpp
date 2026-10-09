#include "ui/DockLayout.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace DockLayout {

// Default split fractions used until ImGui's layout .ini supplies real geometry.
static constexpr float kDefaultRightRatio = 0.24f;
static constexpr float kDefaultStripRatio = 0.09f;
static constexpr float kDefaultPluginsShareOfRight = 0.38f;

static void buildDefaultLayout(ImGuiID dockspaceId) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

  const float rightRatio = kDefaultRightRatio;
  const float stripRatio = kDefaultStripRatio;

  ImGuiID dockMain = dockspaceId;
  ImGuiID dockRight = 0;
  ImGuiID dockBottom = 0;
  ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, stripRatio, &dockBottom, &dockMain);
  ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, rightRatio, &dockRight, &dockMain);

  ImGuiID dockPlugins = 0;
  ImGuiID dockParams = 0;
  ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, kDefaultPluginsShareOfRight, &dockPlugins, &dockParams);

  ImGui::DockBuilderDockWindow(kLeft, dockPlugins);
  ImGui::DockBuilderDockWindow(kParams, dockParams);
  ImGui::DockBuilderDockWindow(kFilmstrip, dockBottom);
  ImGui::DockBuilderDockWindow(kPreview, dockMain);

  if (ImGuiDockNode *previewNode = ImGui::DockBuilderGetNode(dockMain))
    previewNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;

  ImGui::DockBuilderFinish(dockspaceId);
}

void BeginMainDockSpace(App &app) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                               ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##DockHost", nullptr, hostFlags);
  ImGui::PopStyleVar(3);

  const ImGuiID dockspaceId = ImGui::GetID("OfxRawHostDock");
  const bool missingNode = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
  if (missingNode || app.gui.layoutApplyPending) {
    buildDefaultLayout(dockspaceId);
    app.gui.layoutApplyPending = false;
  }

  ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
  ImGui::End();
}

}  // namespace DockLayout
