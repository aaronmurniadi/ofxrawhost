#include "ui/UiContext.h"

#include "imgui.h"

#if defined(__APPLE__)
#include "ui/MacPinch.h"
#endif

#include <algorithm>
#include <cstdio>

void drawPreviewPanel(App &app) {
  const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
  const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  if (canvasSize.x < 1.0f || canvasSize.y < 1.0f) return;

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 canvasMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
  dl->AddRectFilled(canvasPos, canvasMax, ImGui::GetColorU32(ImGuiCol_WindowBg));

  ImGui::InvisibleButton("##previewCanvas", canvasSize);
  const bool canvasHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  const bool active = ImGui::IsItemActive();

  if (!app.tex) {
    dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), ImGui::GetColorU32(ImGuiCol_Text),
                "Open an image to preview.");
    return;
  }

  const float fit = std::min(canvasSize.x / (float)app.texW, canvasSize.y / (float)app.texH);
  const float dispW = app.texW * fit * app.previewZoom;
  const float dispH = app.texH * fit * app.previewZoom;
  const ImVec2 p0(canvasPos.x + canvasSize.x * 0.5f + app.previewPanX - dispW * 0.5f,
                  canvasPos.y + canvasSize.y * 0.5f + app.previewPanY - dispH * 0.5f);
  const ImVec2 p1(p0.x + dispW, p0.y + dispH);
  dl->PushClipRect(canvasPos, canvasMax, true);
  dl->AddImage((ImTextureID)(intptr_t)app.tex, p0, p1);
  dl->PopClipRect();
  char zoomLbl[32];
  std::snprintf(zoomLbl, sizeof zoomLbl, "%.0f%%", app.previewZoom * 100.0f);
  dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + canvasSize.y - ImGui::GetTextLineHeight() - 8.0f),
              ImGui::GetColorU32(ImGuiCol_TextDisabled), zoomLbl);

#if defined(__APPLE__)
  const float pinch = MacPinch_Consume();
#else
  const float pinch = 0.0f;
#endif
  if (canvasHovered) {
    float zoomFactor = 1.0f;
#if defined(__APPLE__)
    if (pinch != 0.0f) {
      zoomFactor *= (1.0f + pinch);
    } else {
      constexpr float kPanScale = 10.0f;
      app.previewPanX += ImGui::GetIO().MouseWheelH * kPanScale;
      app.previewPanY += ImGui::GetIO().MouseWheel * kPanScale;
    }
#else
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) zoomFactor *= (wheel > 0.0f ? 1.1f : 1.0f / 1.1f);
#endif
    if (zoomFactor != 1.0f) {
      const float oldZoom = app.previewZoom;
      app.previewZoom = std::clamp(app.previewZoom * zoomFactor, 0.05f, 64.0f);
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const float ox = canvasPos.x + canvasSize.x * 0.5f + app.previewPanX;
      const float oy = canvasPos.y + canvasSize.y * 0.5f + app.previewPanY;
      const float oldW = app.texW * fit * oldZoom, oldH = app.texH * fit * oldZoom;
      const float newW = app.texW * fit * app.previewZoom, newH = app.texH * fit * app.previewZoom;
      const float u = oldW > 0.0f ? (mouse.x - (ox - oldW * 0.5f)) / oldW : 0.5f;
      const float v = oldH > 0.0f ? (mouse.y - (oy - oldH * 0.5f)) / oldH : 0.5f;
      app.previewPanX += (u - 0.5f) * (oldW - newW);
      app.previewPanY += (v - 0.5f) * (oldH - newH);
    }
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      app.previewZoom = 1.0f;
      app.previewPanX = 0.0f;
      app.previewPanY = 0.0f;
    }
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    app.previewPanX += ImGui::GetIO().MouseDelta.x;
    app.previewPanY += ImGui::GetIO().MouseDelta.y;
  }
}
