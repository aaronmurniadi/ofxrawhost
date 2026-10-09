#include "ui/UiFrame.h"

#include "NodeGraph.h"
#include "ofx/OfxHost.h"
#include "ui/ParamWidgets.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <string>

static void drawParamSearch(App &app) {
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_F))
    ImGui::SetKeyboardFocusHere();
  const bool hasFilter = app.gui.paramFilter[0] != '\0';
  const float clearW = hasFilter ? ImGui::GetFrameHeight() : 0.0f;
  const float clearGap = hasFilter ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearW - clearGap);
  ImGui::InputTextWithHint("##paramFilter", "Search parameters...", app.gui.paramFilter, sizeof app.gui.paramFilter);
  if (hasFilter) {
    ImGui::SameLine();
    if (iconBtn("##clearFilter", ICON_FA_XMARK)) app.gui.paramFilter[0] = '\0';
  }
}

void drawRightPanel(App &app) {
  if (app.chain.nodes.empty()) {
    ImGui::TextDisabled("Add a plugin to edit parameters.");
    return;
  }

  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) app.chain.selectedNode = 0;

  drawParamSearch(app);
  ImGui::Separator();

  if (ImGui::BeginTabBar("##paramNodeTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (int i = 0; i < (int)app.chain.nodes.size(); ++i) {
      const Node &node = app.chain.nodes[i];
      const std::string tabLabel = gPlugins[node.pluginIndex].label + "##tab" + std::to_string(i);
      ImGuiTabItemFlags tabFlags = 0;
      if (app.chain.selectedNode == i) tabFlags |= ImGuiTabItemFlags_SetSelected;
      if (ImGui::BeginTabItem(tabLabel.c_str(), nullptr, tabFlags)) {
        app.chain.selectedNode = i;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }

  ImGui::Separator();
  ImGui::BeginChild("paramsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
  Node *node = selectedNode(app);
  if (node) drawParams(app, *node, "");
  ImGui::EndChild();
}
