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

// One row of actions above the parameter list: the spektrafilm preset import when
// the plugin carries that format, and a reset of every parameter to its OFX
// default. The reset opens a confirmation modal, because one click would discard
// the whole edit.
static void drawParamActions(App &app, Node &node) {
  if (!node.instance) return;
  bool hasPresetImport = false;
  if (node.pluginIndex >= 0 && node.pluginIndex < (int)gPlugins.size()) {
    OfxPlugin *plugin = gPlugins[node.pluginIndex].plugin;
    if (plugin) hasPresetImport = isSpektrafilmPlugin(plugin->pluginIdentifier);
  }

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float full = ImGui::GetContentRegionAvail().x;
  float buttonW = full;
  if (hasPresetImport) buttonW = (full - gap) * 0.5f;

  if (hasPresetImport) {
    if (ImGui::Button("Import *.spkpreset", ImVec2(buttonW, 0.0f))) {
      auto files = pfd::open_file("Import spektrafilm preset", "", {"spektrafilm preset", "*.spkpreset"}).result();
      if (!files.empty()) {
        std::string error;
        std::vector<Param *> changed = applySpektrafilmPreset(files.front(), node.instance.get(), error);
        if (!error.empty()) {
          app.setStatus("Preset import failed: " + error);
        } else if (changed.empty()) {
          app.setStatus("Preset has no parameters this plugin knows");
        } else {
          for (Param *p : changed) notifyChanged(node, p);
          syncChainColorSpaces(app);
          scheduleRender(app);
          app.setStatus("Imported " + std::to_string(changed.size()) + " parameters from " +
                        std::filesystem::path(files.front()).filename().string());
        }
      }
    }
    ImGui::SameLine(0.0f, gap);
  }

  if (ImGui::Button("Reset Parameters", ImVec2(buttonW, 0.0f))) app.gui.showResetParamsConfirm = true;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("Return every parameter of this plugin to its default value.");
}

void drawParametersPanel(App &app) {
  if (app.chain.nodes.empty()) {
    ImGui::TextDisabled("Add a plugin to edit parameters.");
    return;
  }

  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) app.chain.selectedNode = 0;

  drawParamSearch(app);
  ImGui::Separator();

  // The node list in the Plugins panel selects which plugin to edit, so this
  // panel only names the plugin whose parameters it shows.
  Node *node = selectedNode(app);
  if (node) ImGui::SeparatorText(gPlugins[node->pluginIndex].label.c_str());
  if (node) {
    drawParamActions(app, *node);
    ImGui::Separator();
  }

  ImGui::BeginChild("paramsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
  if (node) drawParams(app, *node, "");
  ImGui::EndChild();
}
