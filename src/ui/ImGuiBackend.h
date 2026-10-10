#pragma once

#include <string>

struct GLFWwindow;

// legacyGl selects the fixed-function OpenGL2 backend for contexts without
// OpenGL 3.3, which a virtual machine or a remote desktop session may report.
void ImGuiBackend_Init(GLFWwindow *window, bool legacyGl, int themeIndex, float uiFontSizePt);
// Rebuilds the font atlas at a new size and with the given font file. An empty
// fontPath keeps the standard system font.
void ImGuiBackend_SetUIFont(GLFWwindow *window, float uiFontSizePt, const std::string &fontPath);
void ImGuiBackend_NewFrame();
void ImGuiBackend_Render();
void ImGuiBackend_Shutdown();
// Switches IniFilename to workspace/.ofxrawhost-layout.ini and loads it if present.
// Returns true if a layout file was loaded (caller should not force DockBuilder).
bool ImGuiBackend_SetWorkspaceIni(const std::string &workspaceDir);
