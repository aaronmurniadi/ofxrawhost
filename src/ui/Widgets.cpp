#include "ui/Widgets.h"

#include "imgui.h"

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
  const ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive()   ? ImGuiCol_ButtonActive
                                      : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered
                                                               : ImGuiCol_Button);
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
