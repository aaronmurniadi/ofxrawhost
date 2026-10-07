#include "ui/DockLayout.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>

namespace DockLayout {

static void buildDefaultLayout(App &app, ImGuiID dockspaceId) {
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

  const float refW = std::max(1.0f, vp->WorkSize.x);
  const float refH = std::max(1.0f, vp->WorkSize.y);
  const float leftRatio = std::clamp(app.gui.leftW / refW, 0.12f, 0.40f);
  const float rightRatio = std::clamp(app.gui.rightW / refW, 0.12f, 0.45f);
  const float stripRatio = std::clamp(app.gui.filmstripH / refH, 0.08f, 0.35f);

  ImGuiID dockMain = dockspaceId;
  ImGuiID dockLeft = 0;
  ImGuiID dockRight = 0;
  ImGuiID dockBottom = 0;
  ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, leftRatio, &dockLeft, &dockMain);
  ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, rightRatio / (1.0f - leftRatio), &dockRight, &dockMain);
  ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, stripRatio, &dockBottom, &dockMain);

  ImGui::DockBuilderDockWindow(kLeft, dockLeft);
  ImGui::DockBuilderDockWindow(kParams, dockRight);
  ImGui::DockBuilderDockWindow(kFilmstrip, dockBottom);
  ImGui::DockBuilderDockWindow(kPreview, dockMain);
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
    buildDefaultLayout(app, dockspaceId);
    app.gui.layoutApplyPending = false;
  }

  ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
  ImGui::End();
}

}  // namespace DockLayout
