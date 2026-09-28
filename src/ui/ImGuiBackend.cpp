#include "ui/ImGuiBackend.h"

#include "ui/Themes.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string gIniPath = "ofxrawhost.ini";

void ImGuiBackend_SetDefaultIni() {
  gIniPath = "ofxrawhost.ini";
  if (ImGui::GetCurrentContext()) ImGui::GetIO().IniFilename = gIniPath.c_str();
}

bool ImGuiBackend_SetWorkspaceIni(const std::string &workspaceDir) {
  if (workspaceDir.empty()) {
    ImGuiBackend_SetDefaultIni();
    return false;
  }
  if (ImGui::GetCurrentContext()) ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
  gIniPath = (fs::path(workspaceDir) / ".ofxrawhost-layout.ini").string();
  if (!ImGui::GetCurrentContext()) return false;
  ImGui::GetIO().IniFilename = gIniPath.c_str();
  if (fs::exists(gIniPath)) {
    ImGui::LoadIniSettingsFromDisk(gIniPath.c_str());
    return true;
  }
  return false;
}

bool ImGuiBackend_Init(GLFWwindow *window, int themeIndex) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = gIniPath.c_str();

  // Rasterize fonts at framebuffer DPI so Retina text stays sharp.
  float dpiX = 1.0f, dpiY = 1.0f;
  glfwGetWindowContentScale(window, &dpiX, &dpiY);
  const float dpi = std::max(1.0f, std::max(dpiX, dpiY));
  ImFontConfig fontCfg;
  fontCfg.SizePixels = std::round(13.0f * dpi);
  fontCfg.OversampleH = 2;
  fontCfg.OversampleV = 2;
  io.Fonts->Clear();
  io.Fonts->AddFontDefault(&fontCfg);
#if defined(__APPLE__)
  {
    // Merge the ⌘ glyph so macOS menu shortcuts can render it.
    ImFontConfig symbolsCfg;
    symbolsCfg.MergeMode = true;
    symbolsCfg.PixelSnapH = true;
    symbolsCfg.OversampleH = 2;
    symbolsCfg.OversampleV = 2;
    static const ImWchar symbolRanges[] = {0x2318, 0x2318, 0};
    const char *symCands[] = {
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
    bool symLoaded = false;
    for (const char *path : symCands) {
      if (!path || !fs::exists(path)) continue;
      if (io.Fonts->AddFontFromFileTTF(path, fontCfg.SizePixels, &symbolsCfg, symbolRanges)) {
        symLoaded = true;
        break;
      }
    }
    if (!symLoaded) std::fprintf(stderr, "warning: could not load symbol font for shortcut glyphs\n");
  }
#endif
  {
    ImFontConfig iconsCfg;
    iconsCfg.MergeMode = true;
    iconsCfg.PixelSnapH = true;
    iconsCfg.GlyphMinAdvanceX = fontCfg.SizePixels;
    iconsCfg.OversampleH = 2;
    iconsCfg.OversampleV = 2;
    static const ImWchar iconRanges[] = {0xf00d, 0xf00d, 0xf053, 0xf055, 0xf062, 0xf063, 0xf077, 0xf078, 0xf06e, 0xf070, 0};
    const char *cands[] = {
        OFX_ICON_FONT_PATH,
        "fa-solid-900.ttf",
        "../Resources/fa-solid-900.ttf",
    };
    bool loaded = false;
    for (const char *path : cands) {
      if (!path || !path[0] || !fs::exists(path)) continue;
      if (io.Fonts->AddFontFromFileTTF(path, fontCfg.SizePixels, &iconsCfg, iconRanges)) {
        loaded = true;
        break;
      }
    }
    if (!loaded) std::fprintf(stderr, "warning: could not load Font Awesome icon font\n");
  }
  io.FontGlobalScale = 1.0f / dpi;
  applyTheme(themeIndex);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#if defined(__APPLE__)
  ImGui_ImplOpenGL3_Init("#version 150");
#else
  ImGui_ImplOpenGL3_Init("#version 330");
#endif
  return true;
}

void ImGuiBackend_NewFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiBackend_Render() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiBackend_Shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}
