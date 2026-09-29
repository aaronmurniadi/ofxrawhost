#include "ui/UiFrame.h"

#include "NodeGraph.h"
#include "ui/ParamWidgets.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

void drawRightPanel(App &app) {
  Node *node = selectedNode(app);
  if (!node) {
    ImGui::TextDisabled("Select a node to edit parameters.");
    return;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(gPlugins[node->pluginIndex].label.c_str());
  ImGui::Separator();
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_F))
    ImGui::SetKeyboardFocusHere();
  const bool hasFilter = app.paramFilter[0] != '\0';
  if (hasFilter) {
    const float clearW = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x);
  } else {
    ImGui::SetNextItemWidth(-1);
  }
  ImGui::InputTextWithHint("##paramFilter", "Search parameters...", app.paramFilter, sizeof app.paramFilter);
  if (hasFilter) {
    ImGui::SameLine();
    if (iconBtn("##clearFilter", ICON_FA_XMARK)) app.paramFilter[0] = '\0';
  }
  ImGui::Separator();
  drawParams(app, *node, "");
}
