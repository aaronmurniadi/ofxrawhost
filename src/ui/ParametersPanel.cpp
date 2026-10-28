#include "ui/UiFrame.h"

#include "services/NodeGraph.h"
#include "ofx/OfxHost.h"
#include "ui/ParamWidgets.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

// One row of actions above the parameter list: a reset of every parameter to its
// OFX default. The reset opens a confirmation modal, because one click would
// discard the whole edit. Preset loading lives on the plugin's own Load Preset
// button, which the host wires to its file dialog (see ParamWidgets.cpp).
static void drawParamActions(Workspace &app, Node &node) {
  if (!node.instance) return;
  if (ImGui::Button("Reset Parameters", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    app.gui.showResetParamsConfirm = true;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("Return every parameter of this plugin to its default value.");
}

static void drawParamSearch(Workspace &app) {
  if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_F))
    ImGui::SetKeyboardFocusHere();
  const bool hasFilter = app.gui.paramFilter[0] != '\0';
  // The clear button and the gap before it only take room while a filter is set.
  float clearW = 0.0f;
  float clearGap = 0.0f;
  if (hasFilter) {
    clearW = ImGui::GetFrameHeight();
    clearGap = ImGui::GetStyle().ItemSpacing.x;
  }
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearW - clearGap);
  ImGui::InputTextWithHint("##paramFilter", "Search parameters...", app.gui.paramFilter, sizeof app.gui.paramFilter);
  if (hasFilter) {
    ImGui::SameLine();
    if (iconBtn("##clearFilter", ICON_FA_XMARK)) app.gui.paramFilter[0] = '\0';
  }
}

void drawParametersPanel(Workspace &app) {
  if (app.chain.nodes.empty()) {
    ImGui::TextDisabled("Add a plugin to edit parameters.");
    return;
  }

  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) app.chain.selectedNode = 0;

  drawParamSearch(app);
  ImGui::Separator();

  // The node list in the Plugins panel selects which plugin to edit, so this
  // panel only names the plugin whose parameters it shows.
  Node *node = selectedNode(app.session());
  if (node) ImGui::SeparatorText(plugins()[node->pluginIndex].label.c_str());
  if (node) {
    drawParamActions(app, *node);
    ImGui::Separator();
  }

  ImGui::BeginChild("paramsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
  if (node) drawParams(app, *node, "");
  ImGui::EndChild();
}
