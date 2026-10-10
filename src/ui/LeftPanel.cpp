#include "ui/UiFrame.h"

#include "Actions.h"
#include "persist/ProjectPersist.h"
#include "NodeGraph.h"
#include "RenderScheduler.h"
#include "ofx/OfxHost.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "portable-file-dialogs.h"

#include <string>

static void drawPluginPicker(App &app) {
  if (ImGui::Button("Add plugin…", ImVec2(-1, 0))) ImGui::OpenPopup("##addPluginPopup");
  if (ImGui::BeginPopup("##addPluginPopup")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pluginFilter", "Search…", app.gui.pluginFilter, sizeof app.gui.pluginFilter);
    ImGui::Separator();
    if (gPlugins.empty()) {
      ImGui::TextDisabled("No plugins found");
    } else {
      const std::string q = app.gui.pluginFilter;
      std::string curAuthor;
      int shown = 0;
      for (int i = 0; i < (int)gPlugins.size(); ++i) {
        const auto &pe = gPlugins[i];
        if (!q.empty() && !icontains(pe.label, q) && !icontains(pe.author, q) &&
            !(pe.plugin && pe.plugin->pluginIdentifier && icontains(pe.plugin->pluginIdentifier, q)))
          continue;
        if (pe.author != curAuthor) {
          curAuthor = pe.author;
          ImGui::SeparatorText(curAuthor.c_str());
        }
        if (ImGui::Selectable(pe.label.c_str())) {
          addNode(app, i);
          app.gui.pluginFilter[0] = '\0';
          ImGui::CloseCurrentPopup();
        }
        ++shown;
      }
      if (shown == 0) ImGui::TextDisabled("No matches");
    }
    ImGui::EndPopup();
  }
}

static void drawNodeList(App &app) {
  ImGui::BeginChild("nodeList", ImVec2(0, 0), ImGuiChildFlags_Borders);
  if (app.chain.nodes.empty()) ImGui::TextDisabled("No nodes yet.\nAdd a plugin to build a chain.");
  const float btnH = ImGui::GetFrameHeight();
  const float btnGap = ImGui::GetStyle().ItemSpacing.x;
  const float btnsW = 4.0f * btnH + 3.0f * btnGap;
  int removeAt = -1;
  for (int i = 0; i < (int)app.chain.nodes.size(); ++i) {
    ImGui::PushID(i);
    Node &node = app.chain.nodes[i];
    const bool selected = app.chain.selectedNode == i;
    const std::string label = gPlugins[node.pluginIndex].label;
    if (!node.enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(ImGui::GetContentRegionAvail().x - btnsW - btnGap, btnH))) {
      app.chain.selectedNode = i;
      app.gui.paramFilter[0] = '\0';
    }
    if (!node.enabled) ImGui::PopStyleVar();
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      ImGui::SetDragDropPayload("NODE_IDX", &i, sizeof(i));
      ImGui::Text("%s", label.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NODE_IDX")) {
        const int from = *(const int *)payload->Data;
        moveNode(app, from, i);
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0.0f, btnGap);
    if (iconBtn("##en", node.enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) setNodeEnabled(app, i, !node.enabled);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
      ImGui::SetTooltip(node.enabled ? "Disable processing" : "Enable processing");
    ImGui::SameLine(0.0f, btnGap);
    if (iconBtn("##up", ICON_FA_ARROW_UP) && i > 0) moveNode(app, i, i - 1);
    ImGui::SameLine(0.0f, btnGap);
    if (iconBtn("##dn", ICON_FA_ARROW_DOWN) && i + 1 < (int)app.chain.nodes.size()) moveNode(app, i, i + 1);
    ImGui::SameLine(0.0f, btnGap);
    if (iconBtn("##rm", ICON_FA_XMARK)) removeAt = i;
    ImGui::PopID();
  }
  if (removeAt >= 0) destroyNode(app, removeAt);
  ImGui::EndChild();
}

void drawLeftPanel(App &app) {
  if (ImGui::Button("Open image")) {
    auto f = pfd::open_file("Open image", "", openImageDialogFilters());
    auto r = f.result();
    if (!r.empty()) openPath(app, r[0]);
  }
  ImGui::SameLine();
  if (ImGui::Button("Open Workspace")) {
    auto f = pfd::select_folder("Open workspace folder");
    auto r = f.result();
    if (!r.empty()) app.gui.pendingWorkspaceDir = r;
  }
  ImGui::SameLine();
  if (ImGui::Button("Export")) exportImage(app);

  int outTag = static_cast<int>(app.outputTag.load());
  if (ImGui::Combo("Output tag", &outTag, kOutputSpaces, kOutputSpaceCount)) setOutputTag(app, outTag);
  {
    const char *items[kPreviewResCount];
    for (int i = 0; i < kPreviewResCount; ++i) items[i] = kPreviewRes[i].label;
    int res = static_cast<int>(app.gui.previewRes);
    if (ImGui::Combo("Preview", &res, items, kPreviewResCount)) setPreviewRes(app, res);
  }
  // Export format, bit depth, and quality are chosen in the export dialog.
  ImGui::Separator();
  const std::string status = app.getStatus();
  ImGui::TextWrapped("%s", status.c_str());
  ImGui::Separator();

  ImGui::TextUnformatted("OFX Plugin Nodes");
  drawPluginPicker(app);
  drawNodeList(app);
}
