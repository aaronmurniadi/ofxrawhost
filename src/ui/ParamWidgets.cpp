#include "ui/ParamWidgets.h"

#include "core/RenderContext.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "engine/RenderWorker.h"
#include "services/PluginPresets.h"
#include "services/ParamEdit.h"
#include "ui/Widgets.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ofxParam.h"
#include "portable-file-dialogs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The spektrafilm plugin exposes push buttons that need a file dialog, but the
// plugin carries no dialog code of its own. The host asks for the path here and
// services/PluginPresets does the work, so the rule stays out of the widgets.
// Other plugins open their own dialogs or expose no file control, so this hook
// stays specific to spektrafilm.

// Opens the preset picker and applies the chosen file. A cancelled dialog leaves
// the instance untouched, because the plugin's own button would otherwise
// overwrite the edit with whatever its dropdown holds.
static void importSpektrafilmPreset(Workspace &app, Node &node) {
  std::vector<std::string> files =
      pfd::open_file("Load spektrafilm preset", "", {"spektrafilm preset", presetDialogFilter()}).result();
  if (files.empty()) return;

  const PresetImportResult result = importSpektrafilmPreset(node, files.front());
  app.reportStatus(result.message);
  if (result.changed <= 0) return;
  syncChainColorSpaces(app);
  app.scheduleRender();
}

// Asks for a .cube destination, runs the plugin export, then moves the file the
// plugin wrote to the chosen path. A cancelled dialog touches nothing.
static void exportSpektrafilmLut(Workspace &app, Node &node, Param *button) {
  const Param *identifier = findParam(node.instance.get(), "lutIdentifier");
  std::string stem;
  if (identifier) stem = identifier->s;
  if (stem.empty()) stem = "spektrafilm_lut";

  std::string target =
      pfd::save_file("Export spektrafilm LUT", stem + ".cube", {"3D LUT", lutDialogFilter()}).result();
  if (target.empty()) return;
  if (std::filesystem::path(target).extension() != ".cube") target += ".cube";

  const LutExportResult result = runSpektrafilmLutExport(node, button);
  if (result.produced.empty()) {
    // The plugin reports its own failure, so speak only when it stayed silent.
    if (!result.reported) app.reportStatus("The plugin did not report an exported LUT file");
    return;
  }
  app.reportStatus(placeLutExport(result.produced, target));
}

// True for a string parameter the plugin marks as a file or directory path, so
// the host adds a browse button. This is the OFX convention for a host-owned
// path picker (ofxParam.h, kOfxParamPropStringMode).
static bool isPathString(const Param *p, bool &directory) {
  const std::string mode = sprop(p->props, kOfxParamPropStringMode);
  directory = mode == kOfxParamStringIsDirectoryPath;
  return directory || mode == kOfxParamStringIsFilePath;
}

// DaVinci-style row: label | control/slider | value box | reset.
// The label column follows the panel width, so widening the Parameters
// panel gives a long parameter name more room. It stays between a floor that
// keeps the name readable and a ceiling that leaves the slider and the value
// box usable.
static constexpr float kLabelMinW = 140.0f;
static constexpr float kLabelMaxW = 320.0f;
static constexpr float kLabelShare = 0.32f;
static constexpr float kValueBoxW = 64.0f;

static ImGuiTableFlags paramTableFlags() {
  return ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX |
         ImGuiTableFlags_NoBordersInBodyUntilResize;
}

static float paramLabelWidth() {
  const float avail = ImGui::GetContentRegionAvail().x;
  float w = avail * kLabelShare;
  if (w < kLabelMinW) w = kLabelMinW;
  if (w > kLabelMaxW) w = kLabelMaxW;
  return w;
}

// Draws the label centered in a frame-height cell. A name wider than the cell
// gets an ellipsis instead of a hard clip, and the full name shows in a
// tooltip, so a long name is readable without widening the panel first.
static void tableLabelCell(const char *text) {
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = ImGui::GetFrameHeight();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  // Same vertical placement AlignTextToFramePadding gives a plain text item.
  const ImVec2 textPos(p0.x, p0.y + (h - textSize.y) * 0.5f);
  ImGui::ItemSize(ImVec2(w, h));
  // The label pointer is a stable per-parameter identity, so two parameters
  // with the same label text do not share one item ID.
  ImGui::ItemAdd(ImRect(p0, ImVec2(p0.x + w, p0.y + h)), ImGui::GetID(static_cast<const void *>(text)));
  // RenderTextEllipsis renders the whole text when it fits and an ellipsis when
  // it does not, so the label never spills into the control column.
  ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textPos, ImVec2(p0.x + w, p0.y + h), p0.x + w, p0.x + w,
                            text, nullptr, &textSize);
  if (textSize.x > w && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", text);
}

static void drawResetIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  const float r = (b.x - a.x) * 0.28f;
  constexpr float kPi = 3.14159265f;
  dl->PathClear();
  dl->PathArcTo(c, r, kPi * 0.15f, kPi * 1.75f, 16);
  dl->PathStroke(col, 0, 1.4f);
  const float ang = kPi * 0.15f;
  const ImVec2 tip(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r);
  const ImVec2 t1(tip.x - 3.2f, tip.y - 1.2f);
  const ImVec2 t2(tip.x - 1.2f, tip.y + 3.2f);
  dl->AddTriangleFilled(tip, t1, t2, col);
}

static bool resetParamButton(Param *p) {
  const float h = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button("##reset", ImVec2(h, h));
  drawResetIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Reset to default");
  if (!clicked) return false;
  resetParamToDefault(p);
  return true;
}

static bool beginParamRowTable(float labelW, float valueBoxW, float resetW) {
  if (!ImGui::BeginTable("##row", 4, paramTableFlags(), ImVec2(-FLT_MIN, 0.0f))) return false;
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelW);
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, valueBoxW);
  ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, resetW);
  ImGui::TableNextRow();
  return true;
}

// Choice rows: no separate value column (dropdown uses control + value width).
static bool beginParamRowTableChoice(float labelW, float resetW) {
  if (!ImGui::BeginTable("##row", 3, paramTableFlags(), ImVec2(-FLT_MIN, 0.0f))) return false;
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelW);
  ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, resetW);
  ImGui::TableNextRow();
  return true;
}

// Choice dropdown that applies a newly highlighted option at once when the user
// steps through the list with the arrow keys, instead of waiting for Enter or a
// click. The plugin and the preview therefore follow the highlight live. Returns
// true when the value changed.
static bool choiceCombo(const ParamUiCache &ui, int &cur) {
  const int count = (int)ui.choicePtrs.size();
  if (count <= 0) return false;
  // The closed dropdown shows a label that can differ from the choice text, for
  // example a value with its unit.
  const char *preview = nullptr;
  if (cur >= 0 && cur < (int)ui.choiceLabels.size()) preview = ui.choiceLabels[(size_t)cur].c_str();
  int index = cur;
  if (!uiChoiceCombo("##v", preview, ui.choicePtrs.data(), count, &index)) return false;
  cur = index;
  return true;
}

// One row per parameter kind. Each returns true when the value changed, and the
// dispatcher below owns the notify/sync/schedule that follows a change, so the
// per-kind code is only drawing and value writes.

// A host-run push button: the spektrafilm preset and LUT buttons open a file
// dialog instead of notifying the plugin, because the plugin has none.
static bool drawPushButtonRow(Workspace &app, Param *p) {
  const ParamUiCache &ui = p->ui;
  bool changed = false;
  if (ImGui::Button(ui.idLabel.c_str())) {
    Node *node = selectedNode(app);
    if (node && isHostRunButton(*node, p, "loadPreset")) {
      importSpektrafilmPreset(app, *node);
    } else if (node && isHostRunButton(*node, p, "exportLut")) {
      exportSpektrafilmLut(app, *node, p);
    } else {
      changed = true;
    }
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    Node *node = selectedNode(app);
    if (node && isHostRunButton(*node, p, "loadPreset"))
      ImGui::SetTooltip("Choose a .spkpreset file to apply to this plugin.");
    else if (node && isHostRunButton(*node, p, "exportLut"))
      ImGui::SetTooltip("Choose where to save the exported .cube LUT.");
  }
  return changed;
}

static bool drawChoiceRow(Param *p, float labelW, float resetW) {
  if (!beginParamRowTableChoice(labelW, resetW)) return false;
  const ParamUiCache &ui = p->ui;
  ImGui::TableSetColumnIndex(0);
  tableLabelCell(ui.label.c_str());
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
  int cur = (int)paramScalar(p, 0);
  bool changed = false;
  if (choiceCombo(ui, cur)) {
    setParamScalar(p, 0, cur);
    changed = true;
  }
  ImGui::TableSetColumnIndex(2);
  if (resetParamButton(p)) changed = true;
  ImGui::EndTable();
  return changed;
}

// Double and integer rows: a slider across the display range, an editable value
// box clamped to the hard range, and a reset.
static bool drawScalarRow(Param *p, float labelW, float valueBoxW, float btn) {
  const ParamUiCache &ui = p->ui;
  if (!beginParamRowTable(labelW, valueBoxW, btn)) return false;
  const bool isInt = p->kind == ParamType::Integer;
  const double lo = ui.displayMin;
  const double hi = ui.displayMax;
  const double hardLo = ui.hardMin;
  const double hardHi = ui.hardMax;
  bool changed = false;

  ImGui::TableSetColumnIndex(0);
  tableLabelCell(ui.label.c_str());
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
  double v = paramScalar(p, 0);
  if (isInt) {
    int iv = (int)std::lround(v);
    if (ImGui::SliderInt("##slider", &iv, (int)lo, (int)hi, "")) {
      v = iv;
      changed = true;
    }
  } else {
    float fv = (float)v;
    if (ImGui::SliderFloat("##slider", &fv, (float)lo, (float)hi, "")) {
      v = fv;
      changed = true;
    }
  }

  ImGui::TableSetColumnIndex(2);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (isInt) {
    int iv = (int)std::lround(v);
    if (ImGui::InputInt("##value", &iv, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
      v = std::clamp((double)iv, hardLo, hardHi);
      changed = true;
    }
  } else {
    if (ImGui::InputDouble("##value", &v, 0, 0, "%.4g", ImGuiInputTextFlags_EnterReturnsTrue)) {
      v = std::clamp(v, hardLo, hardHi);
      changed = true;
    }
  }
  if (changed) {
    if (isInt) setParamScalar(p, 0, std::round(v));
    else setParamScalar(p, 0, v);
  }

  ImGui::TableSetColumnIndex(3);
  if (resetParamButton(p)) changed = true;
  ImGui::EndTable();
  return changed;
}

static bool drawBoolRow(Param *p, float labelW, float valueBoxW, float btn) {
  if (!beginParamRowTable(labelW, valueBoxW, btn)) return false;
  ImGui::TableSetColumnIndex(0);
  tableLabelCell(p->ui.label.c_str());
  ImGui::TableSetColumnIndex(1);
  bool v = paramScalar(p, 0) != 0;
  bool changed = false;
  if (ImGui::Checkbox("##v", &v)) {
    if (v) setParamScalar(p, 0, 1.0);
    else setParamScalar(p, 0, 0.0);
    changed = true;
  }
  ImGui::TableSetColumnIndex(3);
  if (resetParamButton(p)) changed = true;
  ImGui::EndTable();
  return changed;
}

// A string row, with a browse button when the plugin marks the value as a file
// or directory path (the OFX convention for a host-owned picker).
static bool drawStringRow(Param *p, float labelW, float valueBoxW, float btn) {
  if (!beginParamRowTable(labelW, valueBoxW, btn)) return false;
  ImGui::TableSetColumnIndex(0);
  tableLabelCell(p->ui.label.c_str());
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s", paramText(p).c_str());
  bool directoryPath = false;
  const bool pathString = isPathString(p, directoryPath);
  bool changed = false;
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputText("##v", buf, sizeof buf)) {
    setParamString(p, buf);
    changed = true;
  }
  if (pathString) {
    ImGui::TableSetColumnIndex(2);
    const char *browseIcon = ICON_FA_FILE_IMPORT;
    if (directoryPath) browseIcon = ICON_FA_FOLDER_OPEN;
    if (iconBtn("##browse", browseIcon)) {
      std::string picked;
      if (directoryPath) {
        picked = pfd::select_folder("Select folder").result();
      } else if (dprop(p->props, kOfxParamPropStringFilePathExists, 0, 1) != 0) {
        std::vector<std::string> files = pfd::open_file("Select file").result();
        if (!files.empty()) picked = files.front();
      } else {
        picked = pfd::save_file("Select file").result();
      }
      if (!picked.empty()) {
        setParamString(p, picked);
        changed = true;
      }
    }
  }
  ImGui::TableSetColumnIndex(3);
  if (resetParamButton(p)) changed = true;
  ImGui::EndTable();
  return changed;
}

// A 2D/3D parameter: one row per component, behind a single label and reset.
static bool drawVectorRows(Param *p, float labelW, float valueBoxW, float btn) {
  const int dim = paramDimension(p->kind);
  const bool isInt = paramIsInteger(p->kind);
  bool changed = false;

  if (!beginParamRowTable(labelW, valueBoxW, btn)) return false;
  ImGui::TableSetColumnIndex(0);
  tableLabelCell(p->ui.label.c_str());
  ImGui::TableSetColumnIndex(3);
  if (resetParamButton(p)) changed = true;
  ImGui::EndTable();

  for (int i = 0; i < dim; ++i) {
    ImGui::PushID(i);
    if (!beginParamRowTable(labelW, valueBoxW, btn)) {
      ImGui::PopID();
      continue;
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    double v = paramScalar(p, i);
    if (isInt) {
      int iv = (int)std::lround(v);
      if (ImGui::DragInt("##d", &iv, 1.0f, 0, 0, "")) {
        setParamScalar(p, i, iv);
        changed = true;
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputInt("##v", &iv, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
        setParamScalar(p, i, iv);
        changed = true;
      }
    } else {
      float fv = (float)v;
      if (ImGui::DragFloat("##d", &fv, 0.01f, 0, 0, "")) {
        setParamScalar(p, i, fv);
        changed = true;
      }
      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-FLT_MIN);
      double dv = fv;
      if (ImGui::InputDouble("##v", &dv, 0, 0, "%.4g", ImGuiInputTextFlags_EnterReturnsTrue)) {
        setParamScalar(p, i, dv);
        changed = true;
      }
    }
    ImGui::EndTable();
    ImGui::PopID();
  }
  return changed;
}

// Dispatch on the kind, then run the one post-edit sequence for every kind.
static void drawParam(Workspace &app, Param *p) {
  const ParamUiCache &ui = p->ui;
  ImGui::PushID(p->name.c_str());
  if (!ui.enabled) ImGui::BeginDisabled();

  const float btn = ImGui::GetFrameHeight();
  const float valueBoxW = kValueBoxW;
  const float labelW = paramLabelWidth();

  bool changed = false;
  if (p->kind == ParamType::PushButton) {
    changed = drawPushButtonRow(app, p);
  } else if (p->kind == ParamType::String && ui.stringIsLabel) {
    ImGui::Text("%s: %s", ui.label.c_str(), paramText(p).c_str());
  } else if (p->kind == ParamType::Choice) {
    changed = drawChoiceRow(p, labelW, btn);
  } else if (p->kind == ParamType::Double || p->kind == ParamType::Integer) {
    changed = drawScalarRow(p, labelW, valueBoxW, btn);
  } else if (p->kind == ParamType::Boolean) {
    changed = drawBoolRow(p, labelW, valueBoxW, btn);
  } else if (p->kind == ParamType::String) {
    changed = drawStringRow(p, labelW, valueBoxW, btn);
  } else if (paramDimension(p->kind) > 1) {
    changed = drawVectorRows(p, labelW, valueBoxW, btn);
  }

  if (!ui.enabled) ImGui::EndDisabled();
  ImGui::PopID();
  if (!changed) return;
  Node *node = selectedNode(app);
  if (node) commitParamEdit(app, *node, p);
}

static bool paramMatches(Param *p, const std::string &q) {
  return icontains(p->ui.label, q) || icontains(p->name, q) || icontains(p->ui.hint, q);
}

static bool subtreeMatches(Effect *e, const std::string &parent, const std::string &q) {
  for (auto &up : e->params) {
    Param *p = up.get();
    if (p->ui.parent != parent || p->kind == ParamType::Page) continue;
    if (p->ui.secret) continue;
    if (p->kind == ParamType::Group) {
      if (paramMatches(p, q) || subtreeMatches(e, p->name, q)) return true;
    } else if (paramMatches(p, q)) {
      return true;
    }
  }
  return false;
}

void drawParams(Workspace &app, Node &node, const std::string &parent) {
  if (!node.instance) return;
  const std::string filter = app.gui.paramFilter;
  const bool filtering = filter[0] != '\0';
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->ui.parent != parent || p->kind == ParamType::Page) continue;
    if (p->ui.secret) continue;
    if (p->kind == ParamType::Group) {
      if (filtering && !subtreeMatches(node.instance.get(), p->name, filter) && !paramMatches(p, filter)) continue;
      const std::string groupLabel = p->ui.label + "##" + p->name;
      if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      else ImGui::SetNextItemOpen(node.groupOpen[p->name], ImGuiCond_Once);
      if (ImGui::CollapsingHeader(groupLabel.c_str())) {
        if (!filtering) node.groupOpen[p->name] = true;
        ImGui::Indent();
        drawParams(app, node, p->name);
        ImGui::Unindent();
      } else if (!filtering) {
        node.groupOpen[p->name] = false;
      }
    } else {
      if (filtering && !paramMatches(p, filter)) continue;
      drawParam(app, p);
    }
  }
}
