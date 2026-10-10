#include "ui/ImGuiBackend.h"

#include "AppState.h"
#include "platform/EmbeddedResource.h"
#include "platform/Paths.h"
#include "platform/SystemUiFont.h"
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

// Path of the ImGui layout .ini. ImGuiBackend_Init points it at the per-user
// config directory; a workspace overrides it through ImGuiBackend_SetWorkspaceIni.
static std::string gIniPath;

// True when the window holds a context without OpenGL 3.3, which the OpenGL2
// backend draws instead. ImGuiBackend_Init sets it.
static bool gLegacyGl = false;

// Loads the bundled Inter font so the interface looks the same on every
// platform, with no dependency on a system font. The build compiles it into the
// Windows executable and copies it next to the binary (or into the bundle
// resources) elsewhere. Falls back to the ImGui built-in font.
static ImFont *addUiFont(ImFontAtlas *fonts, ImFontConfig *cfg) {
  // The atlas owns the buffer handed to AddFontFromMemoryTTF and frees it with
  // the atlas, so hand it a heap copy rather than the read-only image.
  const std::vector<unsigned char> embedded = embeddedResource(kUiFontResourceId);
  if (!embedded.empty()) {
    void *copy = std::malloc(embedded.size());
    if (copy) {
      std::memcpy(copy, embedded.data(), embedded.size());
      if (ImFont *font = fonts->AddFontFromMemoryTTF(copy, (int)embedded.size(), cfg->SizePixels, cfg)) return font;
    }
  }
  const fs::path exe = exeDir();
  const std::vector<std::string> cands = {
      OFX_UI_FONT_PATH,
      (exe / "Inter.ttf").string(),
      (exe / ".." / "Resources" / "Inter.ttf").string(),
      "Inter.ttf",
  };
  for (const std::string &path : cands) {
    if (path.empty() || !fs::exists(path)) continue;
    if (ImFont *font = fonts->AddFontFromFileTTF(path.c_str(), cfg->SizePixels, cfg)) return font;
  }
  std::fprintf(stderr, "warning: could not load the bundled Inter font, using the ImGui default\n");
  return fonts->AddFontDefault(cfg);
}

static void ImGuiBackend_SetDefaultIni() {
  const fs::path dir = userConfigDir();
  if (dir.empty()) {
    gIniPath = "ofxrawhost.ini";
  } else {
    gIniPath = (dir / "layout.ini").string();
  }
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

// Resolves the configured scale against the operating system font size, so a
// new install is the native size and the two other choices scale it.
float resolveUiFontSizePt(int uiFontScale) {
  UiFontScale scale = UiFontScale::Normal;
  if (uiFontScale == static_cast<int>(UiFontScale::Smaller)) scale = UiFontScale::Smaller;
  else if (uiFontScale == static_cast<int>(UiFontScale::Bigger)) scale = UiFontScale::Bigger;
  const float pt = systemUiFontSizePt() * uiFontScaleFactor(scale);
  if (!std::isfinite(pt) || pt < 1.0f) return 11.0f;
  return pt;
}

static void loadUiFonts(GLFWwindow *window, int uiFontScale) {
  ImGuiIO &io = ImGui::GetIO();
  float dpiX = 1.0f, dpiY = 1.0f;
  glfwGetWindowContentScale(window, &dpiX, &dpiY);
  const float dpi = std::max(1.0f, std::max(dpiX, dpiY));
  ImFontConfig fontCfg;
  fontCfg.SizePixels = std::round(resolveUiFontSizePt(uiFontScale) * dpi);
  fontCfg.OversampleH = 2;
  fontCfg.OversampleV = 2;
  io.Fonts->Clear();
  addUiFont(io.Fonts, &fontCfg);
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

void ImGuiBackend_SetUIFont(GLFWwindow *window, int uiFontScale) {
  if (!ImGui::GetCurrentContext() || !window) return;
  loadUiFonts(window, uiFontScale);
  if (gLegacyGl) {
    ImGui_ImplOpenGL2_DestroyFontsTexture();
    ImGui_ImplOpenGL2_CreateFontsTexture();
    return;
  }
  ImGui_ImplOpenGL3_DestroyFontsTexture();
  ImGui_ImplOpenGL3_CreateFontsTexture();
}

void ImGuiBackend_Init(GLFWwindow *window, bool legacyGl, int themeIndex, int uiFontScale) {
  gLegacyGl = legacyGl;
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGuiBackend_SetDefaultIni();

  loadUiFonts(window, uiFontScale);
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
