#include "ui/ParamWidgets.h"

#include "NodeGraph.h"
#include "ui/ParamEdit.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// DaVinci-style row: label | control/slider | value box | reset (fixed columns).
static constexpr float kLabelW = 140.0f;
static constexpr float kValueBoxW = 64.0f;

static ImGuiTableFlags paramTableFlags() {
  return ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX |
         ImGuiTableFlags_NoBordersInBodyUntilResize;
}

static void tableLabelCell(const char *text) {
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = ImGui::GetFrameHeight();
  ImGui::PushClipRect(p0, ImVec2(p0.x + w, p0.y + h), true);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(text);
  ImGui::PopClipRect();
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

static void drawParam(App &app, Param *p) {
  const ParamUiCache &ui = p->ui;
  ImGui::PushID(p->name.c_str());
  if (!ui.enabled) ImGui::BeginDisabled();

  const float btn = ImGui::GetFrameHeight();
  const float valueBoxW = kValueBoxW;
  const float labelW = kLabelW;

  bool changed = false;

  if (p->kind == ParamType::PushButton) {
    if (ImGui::Button(ui.idLabel.c_str())) changed = true;
  } else if (p->kind == ParamType::String && ui.stringIsLabel) {
    ImGui::Text("%s: %s", ui.label.c_str(), paramText(p).c_str());
  } else if (p->kind == ParamType::Choice && beginParamRowTableChoice(labelW, btn)) {
    ImGui::TableSetColumnIndex(0);
    tableLabelCell(ui.label.c_str());
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    int cur = (int)paramScalar(p, 0);
    if (!ui.choicePtrs.empty() && ImGui::Combo("##v", &cur, ui.choicePtrs.data(), (int)ui.choicePtrs.size())) {
      setParamScalar(p, 0, cur);
      changed = true;
    }
    ImGui::TableSetColumnIndex(2);
    if (resetParamButton(p)) changed = true;
    ImGui::EndTable();
  } else if (beginParamRowTable(labelW, valueBoxW, btn)) {
    ImGui::TableSetColumnIndex(0);
    tableLabelCell(ui.label.c_str());

    if (p->kind == ParamType::Double || p->kind == ParamType::Integer) {
      const bool isInt = p->kind == ParamType::Integer;
      const double lo = ui.displayMin, hi = ui.displayMax;
      const double hardLo = ui.hardMin, hardHi = ui.hardMax;

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
      if (changed) setParamScalar(p, 0, isInt ? std::round(v) : v);

      ImGui::TableSetColumnIndex(3);
      if (resetParamButton(p)) changed = true;
    } else if (p->kind == ParamType::Boolean) {
      ImGui::TableSetColumnIndex(1);
      bool v = paramScalar(p, 0) != 0;
      if (ImGui::Checkbox("##v", &v)) {
        setParamScalar(p, 0, v ? 1.0 : 0.0);
        changed = true;
      }
      ImGui::TableSetColumnIndex(3);
      if (resetParamButton(p)) changed = true;
    } else if (p->kind == ParamType::String) {
      char buf[512];
      std::snprintf(buf, sizeof buf, "%s", paramText(p).c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputText("##v", buf, sizeof buf)) {
        setParamString(p, buf);
        changed = true;
      }
      ImGui::TableSetColumnIndex(3);
      if (resetParamButton(p)) changed = true;
    } else if (paramDimension(p->kind) > 1) {
      const int dim = paramDimension(p->kind);
      const bool isInt = paramIsInteger(p->kind);

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
      if (!ui.enabled) ImGui::EndDisabled();
      ImGui::PopID();
      if (changed) {
        Node *node = selectedNode(app);
        if (node) commitParamEdit(app, *node, p);
      }
      return;
    }

    ImGui::EndTable();
  }

  if (!ui.enabled) ImGui::EndDisabled();
  ImGui::PopID();
  if (changed) {
    Node *node = selectedNode(app);
    if (node) commitParamEdit(app, *node, p);
  }
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

void drawParams(App &app, Node &node, const std::string &parent) {
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
