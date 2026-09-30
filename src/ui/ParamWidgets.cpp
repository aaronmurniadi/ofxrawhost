#include "ui/ParamWidgets.h"

#include "NodeGraph.h"
#include "RenderPipeline.h"
#include "ui/Widgets.h"

#include "ofxParam.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static void drawPencilIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const float pad = (b.x - a.x) * 0.22f;
  const ImVec2 p0(a.x + pad, b.y - pad);
  const ImVec2 p1(b.x - pad, a.y + pad);
  const ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
  const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (len < 1.0f) return;
  const ImVec2 n(-dir.y / len * 1.6f, dir.x / len * 1.6f);
  dl->AddLine(ImVec2(p0.x + n.x, p0.y + n.y), ImVec2(p1.x + n.x, p1.y + n.y), col, 1.2f);
  dl->AddLine(ImVec2(p0.x - n.x, p0.y - n.y), ImVec2(p1.x - n.x, p1.y - n.y), col, 1.2f);
  dl->AddLine(p0, ImVec2(p0.x + dir.x * 0.2f, p0.y + dir.y * 0.2f), col, 1.2f);
  dl->AddLine(ImVec2(p1.x + n.x, p1.y + n.y), ImVec2(p1.x - n.x, p1.y - n.y), col, 1.2f);
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

static void drawMinusIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const float cy = (a.y + b.y) * 0.5f;
  const float pad = (b.x - a.x) * 0.28f;
  dl->AddLine(ImVec2(a.x + pad, cy), ImVec2(b.x - pad, cy), col, 1.4f);
}

static void drawPlusIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const float cx = (a.x + b.x) * 0.5f;
  const float cy = (a.y + b.y) * 0.5f;
  const float pad = (b.x - a.x) * 0.28f;
  dl->AddLine(ImVec2(a.x + pad, cy), ImVec2(b.x - pad, cy), col, 1.4f);
  dl->AddLine(ImVec2(cx, a.y + pad), ImVec2(cx, b.y - pad), col, 1.4f);
}

// Param values are shared with in-flight renders; snapshot them under the value lock.
static double paramValue(const Param *p, size_t i) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  return p->v[i];
}

static std::string paramString(const Param *p) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  return p->s;
}

static bool resetParamButton(Param *p) {
  const float h = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button("##reset", ImVec2(h, h));
  drawResetIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Reset to default");
  if (!clicked) return false;
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (p->type == kOfxParamTypeString || p->type == kOfxParamTypeCustom) {
    p->s = sprop(p->props, kOfxParamPropDefault);
    return true;
  }
  for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = dprop(p->props, kOfxParamPropDefault, (int)i, 0);
  return true;
}

static bool paramStepButton(bool plus) {
  const float h = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button(plus ? "##stepup" : "##stepdown", ImVec2(h, h));
  if (plus) drawPlusIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  else drawMinusIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip(plus ? "Increase" : "Decrease");
  return clicked;
}

static bool paramEditButton(double &v, bool asInt, double lo, double hi) {
  const float h = ImGui::GetFrameHeight();
  if (ImGui::Button("##edit", ImVec2(h, h))) ImGui::OpenPopup("##type");
  drawPencilIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Type value");

  if (!ImGui::BeginPopup("##type")) return false;
  ImGui::SetKeyboardFocusHere();
  bool commit = false;
  if (asInt) {
    int iv = (int)std::lround(v);
    if (ImGui::InputInt("##v", &iv, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
      v = std::clamp((double)iv, lo, hi);
      commit = true;
    }
  } else if (ImGui::InputDouble("##v", &v, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
    v = std::clamp(v, lo, hi);
    commit = true;
  }
  if (commit) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return commit;
}

static void drawParam(App &app, Param *p) {
  const std::string &t = p->type;
  const std::string label = sprop(p->props, kOfxPropLabel);
  const std::string idLabel = (label.empty() ? std::string("param") : label) + "##" + p->name;
  const bool enabled = dprop(p->props, kOfxParamPropEnabled, 0, 1) != 0;
  ImGui::PushID(p->name.c_str());
  if (!enabled) ImGui::BeginDisabled();

  const float btn = ImGui::GetFrameHeight();
  const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
  const float labelW = ImGui::CalcTextSize(label.c_str()).x;
  auto valueWidth = [&] { return std::max(40.0f, ImGui::GetContentRegionAvail().x - gap - labelW); };

  bool changed = false;
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
    double lo = dprop(p->props, kOfxParamPropDisplayMin, 0, dprop(p->props, kOfxParamPropMin, 0, 0));
    double hi = dprop(p->props, kOfxParamPropDisplayMax, 0, dprop(p->props, kOfxParamPropMax, 0, t == kOfxParamTypeInteger ? 100 : 1));
    if (!(std::fabs(lo) < 1e7)) lo = 0;
    if (!(std::fabs(hi) < 1e7) || hi <= lo) hi = lo + (t == kOfxParamTypeInteger ? 100 : 1);
    const double hardLo = dprop(p->props, kOfxParamPropMin, 0, lo);
    const double hardHi = dprop(p->props, kOfxParamPropMax, 0, hi);
    const double step = t == kOfxParamTypeInteger ? 1.0 : std::max((hi - lo) / 100.0, 1e-6);
    auto nudge = [&](double d) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      const double v = std::clamp(p->v[0] + d, lo, hi);
      p->v[0] = t == kOfxParamTypeInteger ? std::round(v) : v;
      changed = true;
    };

    if (resetParamButton(p)) changed = true;
    ImGui::SameLine(0, gap);
    double typed = paramValue(p, 0);
    if (paramEditButton(typed, t == kOfxParamTypeInteger, hardLo, hardHi)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = typed;
      changed = true;
    }
    ImGui::SameLine(0, gap);
    if (paramStepButton(false)) nudge(-step);
    ImGui::SameLine(0, gap);
    if (paramStepButton(true)) nudge(step);
    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(valueWidth());
    float fv = (float)paramValue(p, 0);
    if (ImGui::SliderFloat(idLabel.c_str(), &fv, (float)lo, (float)hi)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = t == kOfxParamTypeInteger ? std::round(fv) : fv;
      changed = true;
    }
  } else if (t == kOfxParamTypeBoolean) {
    if (resetParamButton(p)) changed = true;
    ImGui::SameLine(0, gap);
    bool v = paramValue(p, 0) != 0;
    if (ImGui::Checkbox(idLabel.c_str(), &v)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = v ? 1 : 0;
      changed = true;
    }
  } else if (t == kOfxParamTypeChoice) {
    if (resetParamButton(p)) changed = true;
    ImGui::SameLine(0, gap);
    const auto &opts = choiceOptions(p);
    int cur = (int)paramValue(p, 0);
    std::vector<const char *> items;
    items.reserve(opts.size());
    for (auto &o : opts) items.push_back(o.s.c_str());
    ImGui::SetNextItemWidth(valueWidth());
    if (!items.empty() && ImGui::Combo(idLabel.c_str(), &cur, items.data(), (int)items.size())) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = cur;
      changed = true;
    }
  } else if (t == kOfxParamTypePushButton) {
    if (ImGui::Button(idLabel.c_str())) changed = true;
  } else if (t == kOfxParamTypeString) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", paramString(p).c_str());
    const bool editable = sprop(p->props, kOfxParamPropStringMode) != kOfxParamStringIsLabel;
    if (editable) {
      if (resetParamButton(p)) changed = true;
      ImGui::SameLine(0, gap);
      ImGui::SetNextItemWidth(valueWidth());
      if (ImGui::InputText(idLabel.c_str(), buf, sizeof buf)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->s = buf;
        changed = true;
      }
    } else {
      ImGui::Text("%s: %s", label.c_str(), paramString(p).c_str());
    }
  } else if (dims(t) > 1) {
    const float rowW = ImGui::CalcItemWidth();
    if (resetParamButton(p)) changed = true;
    ImGui::SameLine(0, gap);
    ImGui::TextUnformatted(label.c_str());
    ImGui::Indent();
    for (int i = 0; i < dims(t); ++i) {
      const double val = paramValue(p, i);
      float fv = (float)val;
      ImGui::PushID(i);
      double typed = val;
      if (paramEditButton(typed, isIntType(t), -1e7, 1e7)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[i] = typed;
        changed = true;
      }
      ImGui::SameLine(0, gap);
      ImGui::SetNextItemWidth(std::max(40.0f, rowW - btn - gap));
      if (ImGui::DragFloat("##v", &fv, isIntType(t) ? 1.0f : 0.01f)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[i] = isIntType(t) ? std::round(fv) : fv;
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::Unindent();
  }

  if (!enabled) ImGui::EndDisabled();
  ImGui::PopID();
  if (changed) {
    Node *node = selectedNode(app);
    if (node) {
      notifyChanged(*node, p);
      syncOutputTag(app);
      scheduleRender(app);
    }
  }
}

static bool paramMatches(Param *p, const std::string &q) {
  return icontains(sprop(p->props, kOfxPropLabel), q) || icontains(p->name, q) ||
         icontains(sprop(p->props, kOfxParamPropHint), q);
}

static bool ancestorsOpen(Node &node, const std::string &group) {
  if (group.empty()) return true;
  Param *g = findParam(node.instance.get(), group.c_str());
  return g && node.groupOpen[group] && ancestorsOpen(node, sprop(g->props, kOfxParamPropParent));
}

static bool subtreeMatches(Effect *e, const std::string &parent, const std::string &q) {
  for (auto &up : e->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    if (p->type == kOfxParamTypeGroup) {
      if (paramMatches(p, q) || subtreeMatches(e, p->name, q)) return true;
    } else if (paramMatches(p, q)) {
      return true;
    }
  }
  return false;
}

void drawParams(App &app, Node &node, const std::string &parent) {
  if (!node.instance) return;
  const std::string filter = app.paramFilter;
  const bool filtering = filter[0] != '\0';
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    if (p->type == kOfxParamTypeGroup) {
      if (filtering && !subtreeMatches(node.instance.get(), p->name, filter) && !paramMatches(p, filter)) continue;
      if (!filtering && !ancestorsOpen(node, parent) && parent != "") continue;
      const std::string groupLabel = sprop(p->props, kOfxPropLabel) + "##" + p->name;
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
      if (filtering) {
        if (!paramMatches(p, filter)) continue;
      } else if (!ancestorsOpen(node, parent)) {
        continue;
      }
      drawParam(app, p);
    }
  }
}
