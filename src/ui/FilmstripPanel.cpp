#include "ui/UiFrame.h"

#include "Actions.h"
#include "Filmstrip.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

struct FilmstripTabItem {
  FilmstripTab tab;
  const char *label;
};

static const FilmstripTabItem kFilmstripTabs[] = {
    {FilmstripTab::All, "All"},
    {FilmstripTab::RAW, "RAW"},
    {FilmstripTab::Compressed, "Compressed"},
};

void drawFilmstripPanel(App &app) {
  if (app.workspaceDir.empty() || app.filmstrip.empty()) {
    ImGui::TextDisabled("Open a workspace folder to browse images.");
    return;
  }

  if (ImGui::BeginTabBar("##filmstripTabs")) {
    for (const FilmstripTabItem &t : kFilmstripTabs) {
      if (ImGui::BeginTabItem(t.label)) {
        app.filmstripTab = t.tab;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }

  const float thumbH = std::max(24.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().FramePadding.y * 2.0f);
  const float fbScale = std::max(1.0f, ImGui::GetIO().DisplayFramebufferScale.y);
  const int wantEdge = snapFilmstripThumbEdge(thumbH * fbScale);
  if (app.filmstripThumbEdge.exchange(wantEdge) != wantEdge) invalidateFilmstripThumbs(app);

  if (app.filmstripIndex >= 0) requestFilmstripThumb(app, app.filmstripIndex, true);

  // Lay out the strip by hand so off-screen entries cost nothing. Only the entries
  // that intersect the visible band get a widget; a trailing dummy keeps the full
  // content width so the horizontal scrollbar can still reach every entry.
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float startX = ImGui::GetCursorPosX();
  const float scrollX = ImGui::GetScrollX();
  const float viewLeft = scrollX - thumbH;
  const float viewRight = scrollX + ImGui::GetContentRegionAvail().x + thumbH;
  float x = startX;
  bool anyDrawn = false;
  for (int i = 0; i < (int)app.filmstrip.size(); ++i) {
    FilmstripEntry &e = app.filmstrip[i];
    if (app.filmstripTab == FilmstripTab::RAW && !e.isRaw) continue;
    if (app.filmstripTab == FilmstripTab::Compressed && e.isRaw) continue;
    const float w = thumbH * ((e.tex.h && e.tex.w) ? (float)e.tex.w / (float)e.tex.h : 1.0f);
    if (x + w >= viewLeft && x <= viewRight) {
      if (!anyDrawn) ImGui::SetCursorPosX(x);
      else ImGui::SameLine(x, 0.0f);
      const ImVec2 btnSize(w, thumbH);
      ImGui::PushID(i);
      const bool selected = i == app.filmstripIndex;
      if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
      if (e.tex.id)
        ImGui::ImageButton("##t", (ImTextureID)(intptr_t)e.tex.id, btnSize);
      else
        ImGui::Button(e.thumbFailed ? "?" : "…", btnSize);
      if (ImGui::IsItemVisible()) requestFilmstripThumb(app, i, selected);
      if (selected) ImGui::PopStyleColor();
      if (ImGui::IsItemClicked()) openPath(app, e.path);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", fs::path(e.path).filename().string().c_str());
      ImGui::PopID();
      anyDrawn = true;
    }
    x += w + spacing;
  }
  if (anyDrawn) {
    ImGui::SameLine(x - spacing, 0.0f);
    ImGui::Dummy(ImVec2(1.0f, 0.0f));
  }
}
