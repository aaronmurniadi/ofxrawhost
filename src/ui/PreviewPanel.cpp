#include "ui/UiFrame.h"

#include "imgui.h"
#include "imgui_internal.h"  // RenderTextEllipsis, also used by the param labels

#if defined(__APPLE__)
#include "ui/MacPinch.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Padding of the preview corner overlays from the panel edge.
constexpr float kOverlayPad = 8.0f;

// Overlay text drawn on the preview canvas: the input file name in the top-left
// corner and the zoom level in the bottom-left one. Both go through here so the
// two corners cannot drift apart, and a name wider than the panel is ellipsized
// rather than spilling over the image.
static void previewOverlayText(ImDrawList *dl, const ImVec2 pos, float maxWidth, const ImU32 col, const char *text) {
  if (text[0] == '\0') return;
  if (maxWidth < 1.0f) return;
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  // The explicit end keeps a file name containing '##' literal: the helper
  // strips that marker when it has to find the end itself.
  const char *textEnd = text + std::strlen(text);
  // The helper draws with ImGuiCol_Text, so the wanted color is pushed.
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::RenderTextEllipsis(dl, pos, ImVec2(pos.x + maxWidth, pos.y + textSize.y), pos.x + maxWidth,
                            pos.x + maxWidth, text, textEnd, &textSize);
  ImGui::PopStyleColor();
}

// A spinner is the only feedback the user gets while a RAW develops, because the
// decode runs on its own thread and the window stays responsive. The arc is
// drawn from the frame clock, so it turns.
static void drawOpeningSpinner(ImDrawList *dl, const ImVec2 center, float radius, ImU32 col) {
  constexpr float kPi = 3.14159265f;
  const float turn = (float)ImGui::GetTime() * 2.6f;
  dl->PathClear();
  dl->PathArcTo(center, radius, turn, turn + kPi * 1.25f, 40);
  dl->PathStroke(col, 0, 3.0f);
}

// The block shown over the canvas while an image is being decoded.
static void drawOpeningState(ImDrawList *dl, const ImVec2 canvasPos, const ImVec2 canvasSize, const std::string &name) {
  const ImVec2 canvasMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
  dl->AddRectFilled(canvasPos, canvasMax, IM_COL32(0, 0, 0, 140));
  const ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
  const ImU32 accent = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  drawOpeningSpinner(dl, ImVec2(center.x, center.y - 12.0f), 16.0f, accent);
  char label[192];
  if (name.empty()) std::snprintf(label, sizeof label, "Opening...");
  else std::snprintf(label, sizeof label, "Opening %s...", name.c_str());
  const ImVec2 textSize = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y + 16.0f), ImGui::GetColorU32(ImGuiCol_Text), label);
}

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

  // The preview render matches this size in framebuffer pixels, so at 100% zoom
  // the image maps to the screen one pixel for one pixel.
  const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
  app.gui.previewAreaW = (int)std::lround(canvasSize.x * fbScale.x);
  app.gui.previewAreaH = (int)std::lround(canvasSize.y * fbScale.y);

  // A decode in flight covers the canvas, so the user sees progress instead of a
  // frozen window. The previous image stays underneath until the new one lands.
  if (app.opener.busy()) {
    const std::string name = std::filesystem::path(app.opener.path()).filename().string();
    drawOpeningState(dl, canvasPos, canvasSize, name);
    return;
  }

  if (!app.previewTexture.id) {
    dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), ImGui::GetColorU32(ImGuiCol_Text),
                "Open an image to preview.");
    return;
  }

  const float fit = std::min(canvasSize.x / (float)app.previewTexture.w, canvasSize.y / (float)app.previewTexture.h);
  const float dispW = app.previewTexture.w * fit * app.gui.previewZoom;
  const float dispH = app.previewTexture.h * fit * app.gui.previewZoom;
  const ImVec2 p0(canvasPos.x + canvasSize.x * 0.5f + app.gui.previewPanX - dispW * 0.5f,
                  canvasPos.y + canvasSize.y * 0.5f + app.gui.previewPanY - dispH * 0.5f);
  const ImVec2 p1(p0.x + dispW, p0.y + dispH);
  dl->PushClipRect(canvasPos, canvasMax, true);
  dl->AddImage((ImTextureID)(intptr_t)app.previewTexture.id, p0, p1);
  dl->PopClipRect();
  char zoomLbl[32];
  std::snprintf(zoomLbl, sizeof zoomLbl, "%.0f%%", app.gui.previewZoom * 100.0f);
  const float overlayTextH = ImGui::GetTextLineHeight();
  const float overlayMaxW = canvasSize.x - kOverlayPad * 2.0f;
  const float zoomY = canvasPos.y + canvasSize.y - overlayTextH - kOverlayPad;
  // On a panel too short to hold both bands the name yields: the zoom level has
  // no other home, and two overlapping labels would be unreadable.
  if (!app.document.path.empty() && canvasPos.y + kOverlayPad + overlayTextH <= zoomY) {
    const std::string name = fs::path(app.document.path).filename().string();
    previewOverlayText(dl, ImVec2(canvasPos.x + kOverlayPad, canvasPos.y + kOverlayPad), overlayMaxW,
                       ImGui::GetColorU32(ImGuiCol_TextDisabled), name.c_str());
  }
  previewOverlayText(dl, ImVec2(canvasPos.x + kOverlayPad, zoomY), overlayMaxW,
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), zoomLbl);

#if defined(__APPLE__)
  const float pinch = MacPinch_Consume();
#endif
  if (canvasHovered) {
    float zoomFactor = 1.0f;
#if defined(__APPLE__)
    if (pinch != 0.0f) {
      zoomFactor *= (1.0f + pinch);
    } else {
      constexpr float kPanScale = 10.0f;
      app.gui.previewPanX += ImGui::GetIO().MouseWheelH * kPanScale;
      app.gui.previewPanY += ImGui::GetIO().MouseWheel * kPanScale;
    }
#else
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel > 0.0f) zoomFactor *= 1.1f;
    else if (wheel < 0.0f) zoomFactor *= 1.0f / 1.1f;
#endif
    if (zoomFactor != 1.0f) {
      const float oldZoom = app.gui.previewZoom;
      app.gui.previewZoom = std::clamp(app.gui.previewZoom * zoomFactor, 0.05f, 64.0f);
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      const float ox = canvasPos.x + canvasSize.x * 0.5f + app.gui.previewPanX;
      const float oy = canvasPos.y + canvasSize.y * 0.5f + app.gui.previewPanY;
      const float oldW = app.previewTexture.w * fit * oldZoom, oldH = app.previewTexture.h * fit * oldZoom;
      const float newW = app.previewTexture.w * fit * app.gui.previewZoom, newH = app.previewTexture.h * fit * app.gui.previewZoom;
      // Keep the image point under the cursor still. Fall back to the centre
      // when the previous frame had no size to map against.
      float u = 0.5f;
      float v = 0.5f;
      if (oldW > 0.0f) u = (mouse.x - (ox - oldW * 0.5f)) / oldW;
      if (oldH > 0.0f) v = (mouse.y - (oy - oldH * 0.5f)) / oldH;
      app.gui.previewPanX += (u - 0.5f) * (oldW - newW);
      app.gui.previewPanY += (v - 0.5f) * (oldH - newH);
    }
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      app.gui.previewZoom = 1.0f;
      app.gui.previewPanX = 0.0f;
      app.gui.previewPanY = 0.0f;
    }
  }
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    app.gui.previewPanX += ImGui::GetIO().MouseDelta.x;
    app.gui.previewPanY += ImGui::GetIO().MouseDelta.y;
  }
}
