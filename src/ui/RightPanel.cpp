#include "ui/UiFrame.h"

#include "NodeGraph.h"
#include "ParamBridge.h"
#include "RenderScheduler.h"
#include "ofx/OfxHost.h"
#include "ofx/SpektraPreset.h"
#include "ui/ParamWidgets.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "portable-file-dialogs.h"

#include <filesystem>
#include <string>
#include <vector>

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

// The spektrafilm plugins carry their own preset format, so the import lives
// next to their parameter list rather than in the generic parameter UI.
static bool drawPresetImport(App &app, Node &node) {
  if (!node.instance) return false;
  if (node.pluginIndex < 0 || node.pluginIndex >= (int)gPlugins.size()) return false;
  OfxPlugin *plugin = gPlugins[node.pluginIndex].plugin;
  if (!isSpektrafilmPlugin(plugin ? plugin->pluginIdentifier : nullptr)) return false;

  if (!ImGui::Button("Import *.spkpreset", ImVec2(-1.0f, 0.0f))) return true;
  auto files = pfd::open_file("Import spektrafilm preset", "", {"spektrafilm preset", "*.spkpreset"}).result();
  if (files.empty()) return true;

  std::string error;
  std::vector<Param *> changed = applySpektrafilmPreset(files.front(), node.instance.get(), error);
  if (!error.empty()) {
    app.setStatus("Preset import failed: " + error);
    return true;
  }
  if (changed.empty()) {
    app.setStatus("Preset has no parameters this plugin knows");
    return true;
  }
  for (Param *p : changed) notifyChanged(node, p);
  syncOutputTag(app);
  scheduleRender(app);
  app.setStatus("Imported " + std::to_string(changed.size()) + " parameters from " +
                std::filesystem::path(files.front()).filename().string());
  return true;
}

void drawRightPanel(App &app) {
  if (app.chain.nodes.empty()) {
    ImGui::TextDisabled("Add a plugin to edit parameters.");
    app.gui.paramTabSync = -1;
    return;
  }

  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) app.chain.selectedNode = 0;

  drawParamSearch(app);
  ImGui::Separator();

  // ImGui applies ImGuiTabItemFlags_SetSelected on the next frame. Pushing it
  // every frame for the selected node fights a tab click and makes the visible
  // tab alternate between the clicked tab and the selected one. Push the flag
  // only when the node list or the graph changed the selection, and let the tab
  // bar drive the selection otherwise.
  const int selected = app.chain.selectedNode;
  const bool selectionFromOutside = (app.gui.paramTabSync != selected);
  if (ImGui::BeginTabBar("##paramNodeTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
    for (int i = 0; i < (int)app.chain.nodes.size(); ++i) {
      const Node &node = app.chain.nodes[i];
      const std::string tabLabel = gPlugins[node.pluginIndex].label + "##tab" + std::to_string(i);
      ImGuiTabItemFlags tabFlags = 0;
      if (selectionFromOutside && selected == i) tabFlags |= ImGuiTabItemFlags_SetSelected;
      if (ImGui::BeginTabItem(tabLabel.c_str(), nullptr, tabFlags)) {
        if (!selectionFromOutside) app.chain.selectedNode = i;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  app.gui.paramTabSync = app.chain.selectedNode;

  ImGui::Separator();
  Node *node = selectedNode(app);
  if (node && drawPresetImport(app, *node)) ImGui::Separator();
  ImGui::BeginChild("paramsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
  if (node) drawParams(app, *node, "");
  ImGui::EndChild();
}
