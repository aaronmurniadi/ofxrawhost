#include "ui/UiContext.h"

#include "DocumentActions.h"
#include "Filmstrip.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

void drawFilmstripPanel(App &app) {
  if (app.workspaceDir.empty() || app.filmstrip.empty()) {
    ImGui::TextDisabled("Open a workspace folder to browse images.");
    return;
  }

  pumpFilmstripThumbs(app);
  if (app.filmstripIndex >= 0) requestFilmstripThumb(app, app.filmstripIndex, true);

  const float thumbH = std::max(24.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().FramePadding.y * 2.0f);
  for (int i = 0; i < (int)app.filmstrip.size(); ++i) {
    FilmstripEntry &e = app.filmstrip[i];
    const float aspect = (e.th && e.tw) ? (float)e.tw / (float)e.th : 1.0f;
    const ImVec2 btnSize(thumbH * aspect, thumbH);
    ImGui::PushID(i);
    const bool selected = i == app.filmstripIndex;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
    if (e.tex)
      ImGui::ImageButton("##t", (ImTextureID)(intptr_t)e.tex, btnSize);
    else
      ImGui::Button(e.thumbFailed ? "?" : "…", btnSize);
    if (ImGui::IsItemVisible()) requestFilmstripThumb(app, i, selected);
    if (selected) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) openPath(app, e.path, true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
      ImGui::SetTooltip("%s", fs::path(e.path).filename().string().c_str());
    ImGui::PopID();
    if (i + 1 < (int)app.filmstrip.size()) ImGui::SameLine();
  }
}
