#include "ui/UiFrame.h"

#include "Actions.h"
#include "Filmstrip.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
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

static bool isRawImagePath(const std::string &path) {
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);
  return e == ".cr2" || e == ".cr3" || e == ".nef" || e == ".arw" || e == ".dng" ||
         e == ".raf" || e == ".orf" || e == ".rw2" || e == ".pef" || e == ".srw" ||
         e == ".raw";
}

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

  pumpFilmstripThumbs(app);
  if (app.filmstripIndex >= 0) requestFilmstripThumb(app, app.filmstripIndex, true);

  for (int i = 0; i < (int)app.filmstrip.size(); ++i) {
    if (app.filmstripTab == FilmstripTab::RAW && !isRawImagePath(app.filmstrip[i].path)) continue;
    if (app.filmstripTab == FilmstripTab::Compressed && isRawImagePath(app.filmstrip[i].path)) continue;
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
