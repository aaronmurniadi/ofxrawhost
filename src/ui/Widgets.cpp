#include "ui/Widgets.h"

#include "imgui.h"
#include "imgui_internal.h"  // ScrollToItem

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>

bool icontains(const std::string &hay, const std::string &needle) {
  if (needle.empty()) return true;
  auto lower = [](unsigned char c) { return (char)std::tolower(c); };
  auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                        [&](char a, char b) { return lower((unsigned char)a) == lower((unsigned char)b); });
  return it != hay.end();
}

static ImWchar utf8Codepoint(const char *s) {
  const unsigned char *u = (const unsigned char *)s;
  if (u[0] < 0x80) return u[0];
  if ((u[0] & 0xE0) == 0xC0) return (ImWchar)(((u[0] & 0x1F) << 6) | (u[1] & 0x3F));
  if ((u[0] & 0xF0) == 0xE0) return (ImWchar)(((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F));
  return 0;
}

bool iconBtn(const char *id, const char *icon) {
  const float h = ImGui::GetFrameHeight();
  ImGui::PushID(id);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton("##", ImVec2(h, h));
  ImGuiCol buttonCol = ImGuiCol_Button;
  if (ImGui::IsItemHovered()) buttonCol = ImGuiCol_ButtonHovered;
  if (ImGui::IsItemActive()) buttonCol = ImGuiCol_ButtonActive;
  const ImU32 bg = ImGui::GetColorU32(buttonCol);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), bg, ImGui::GetStyle().FrameRounding);
  ImFont *font = ImGui::GetFont();
  const float fontSize = ImGui::GetFontSize();
  const ImWchar cp = utf8Codepoint(icon);
  if (cp) {
    const ImVec2 sz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon);
    const float x = p.x + (h - sz.x) * 0.5f;
    const float y = p.y + (h - sz.y) * 0.5f;
    font->RenderChar(dl, fontSize, ImVec2(std::floor(x), std::floor(y)), ImGui::GetColorU32(ImGuiCol_Text), cp);
  }
  ImGui::PopID();
  return pressed;
}

// True when the choice at index can be picked. A null list means every choice
// can be.
static bool choiceAllowed(const bool *enabled, int index) {
  if (!enabled) return true;
  return enabled[index];
}

bool uiChoiceCombo(const char *label, const char *preview, const char *const *labels, int count, int *cur,
                   const bool *enabled) {
  if (!label || !labels || !cur || count <= 0) return false;
  if (*cur < 0) *cur = 0;
  if (*cur >= count) *cur = count - 1;
  const char *shown = preview;
  if (!shown) shown = labels[*cur];

  if (!ImGui::BeginCombo(label, shown)) return false;
  // ImGui moves its highlight on an arrow key but only activates on Enter, so
  // step the value here to apply the highlighted choice immediately. A choice
  // that cannot be picked is stepped over.
  const int current = *cur;
  int stepped = current;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    for (int i = current + 1; i < count; ++i) {
      if (!choiceAllowed(enabled, i)) continue;
      stepped = i;
      break;
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
    for (int i = current - 1; i >= 0; --i) {
      if (!choiceAllowed(enabled, i)) continue;
      stepped = i;
      break;
    }
  }
  const bool moved = stepped != current;
  // A choice far down a long list opens with that row in the middle of the
  // popup, the same way ImGui's own navigation opens a list.
  const bool opening = ImGui::IsWindowAppearing();

  bool changed = false;
  for (int i = 0; i < count; ++i) {
    const bool canPick = choiceAllowed(enabled, i);
    if (!canPick) ImGui::BeginDisabled();
    if (ImGui::Selectable(labels[i], i == current)) {
      *cur = i;
      changed = true;
    }
    if (!canPick) ImGui::EndDisabled();
    // The scroll reaches the screen on the next frame, because the rows of this
    // frame are already placed. It runs only on the frames that move the
    // highlight, so the mouse wheel can still move the list away from it.
    if (moved && i == stepped) {
      ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    } else if (opening && i == *cur) {
      ImGui::ScrollToItem(ImGuiScrollFlags_AlwaysCenterY);
    }
  }
  if (moved) {
    *cur = stepped;
    changed = true;
  }
  ImGui::EndCombo();
  return changed;
}
