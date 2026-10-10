#include "ui/ImGuiBackend.h"

#include "platform/EmbeddedResource.h"
#include "platform/Paths.h"
#include "ui/Themes.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string gIniPath = "ofxrawhost.ini";

// True when the window holds a context without OpenGL 3.3, which the OpenGL2
// backend draws instead. ImGuiBackend_Init sets it.
static bool gLegacyGl = false;

static ImFont *addSystemSansFont(ImFontAtlas *fonts, ImFontConfig *cfg) {
#if defined(__APPLE__)
  const char *cands[] = {
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
  };
#elif defined(_WIN32)
  const char *cands[] = {
      "C:\\Windows\\Fonts\\segoeui.ttf",
      "C:\\Windows\\Fonts\\arial.ttf",
  };
#else
  const char *cands[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
  };
#endif
  for (const char *path : cands) {
    if (!path || !fs::exists(path)) continue;
    if (ImFont *font = fonts->AddFontFromFileTTF(path, cfg->SizePixels, cfg)) return font;
  }
  std::fprintf(stderr, "warning: system sans font not found, using ImGui default\n");
  return fonts->AddFontDefault(cfg);
}

static void ImGuiBackend_SetDefaultIni() {
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

static void loadUiFonts(GLFWwindow *window, float uiFontSizePt) {
  ImGuiIO &io = ImGui::GetIO();
  float dpiX = 1.0f, dpiY = 1.0f;
  glfwGetWindowContentScale(window, &dpiX, &dpiY);
  const float dpi = std::max(1.0f, std::max(dpiX, dpiY));
  ImFontConfig fontCfg;
  fontCfg.SizePixels = std::round(uiFontSizePt * dpi);
  fontCfg.OversampleH = 2;
  fontCfg.OversampleV = 2;
  io.Fonts->Clear();
  addSystemSansFont(io.Fonts, &fontCfg);
#if defined(__APPLE__)
  {
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
    static const ImWchar iconRanges[] = {0xf00d, 0xf00d, 0xf062, 0xf063, 0xf06e, 0xf06e, 0xf070, 0xf070, 0};
    // Packaged builds keep the icon font inside the executable (Windows) or next
    // to it (Linux archive, AppImage, macOS Contents/Resources).
    const fs::path exe = exeDir();
    const std::vector<std::string> cands = {
        OFX_ICON_FONT_PATH,
        (exe / "fa-solid-900.ttf").string(),
        (exe / ".." / "Resources" / "fa-solid-900.ttf").string(),
        "fa-solid-900.ttf",
    };
    bool loaded = false;
    // The atlas owns this buffer and frees it with the atlas, so hand it a copy
    // that came from the heap rather than the read-only image.
    const std::vector<unsigned char> embedded = embeddedResource(kIconFontResourceId);
    if (!embedded.empty()) {
      void *copy = std::malloc(embedded.size());
      if (copy) {
        std::memcpy(copy, embedded.data(), embedded.size());
        if (io.Fonts->AddFontFromMemoryTTF(copy, (int)embedded.size(), fontCfg.SizePixels, &iconsCfg, iconRanges))
          loaded = true;
      }
    }
    for (const std::string &path : cands) {
      if (loaded) break;
      if (path.empty() || !fs::exists(path)) continue;
      if (io.Fonts->AddFontFromFileTTF(path.c_str(), fontCfg.SizePixels, &iconsCfg, iconRanges)) {
        loaded = true;
        break;
      }
    }
    if (!loaded) std::fprintf(stderr, "warning: could not load Font Awesome icon font\n");
  }
  io.FontGlobalScale = 1.0f / dpi;
}

void ImGuiBackend_SetUIFontSize(GLFWwindow *window, float uiFontSizePt) {
  if (!ImGui::GetCurrentContext() || !window) return;
  loadUiFonts(window, uiFontSizePt);
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_DestroyFontsTexture();
    ImGui_ImplOpenGL2_CreateFontsTexture();
    return;
  }
  ImGui_ImplOpenGL3_DestroyFontsTexture();
  ImGui_ImplOpenGL3_CreateFontsTexture();
}

void ImGuiBackend_Init(GLFWwindow *window, bool legacyGl, int themeIndex, float uiFontSizePt) {
  gLegacyGl = legacyGl;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = gIniPath.c_str();

  loadUiFonts(window, uiFontSizePt);
  applyTheme(themeIndex);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_Init();
    return;
  }
#if defined(__APPLE__)
  ImGui_ImplOpenGL3_Init("#version 150");
#else
  ImGui_ImplOpenGL3_Init("#version 330");
#endif
}

void ImGuiBackend_NewFrame() {
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_NewFrame();
  } else {
    ImGui_ImplOpenGL3_NewFrame();
  }
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiBackend_Render() {
  ImGui::Render();
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    return;
  }
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiBackend_Shutdown() {
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_Shutdown();
  } else {
    ImGui_ImplOpenGL3_Shutdown();
  }
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}
