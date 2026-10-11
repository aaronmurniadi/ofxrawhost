#pragma once

#include <string>

struct GLFWwindow;

// legacyGl selects the fixed-function OpenGL2 backend for contexts without
// OpenGL 3.3, which a virtual machine or a remote desktop session may report.
// uiFontPt is the interface size in points, and 0 follows the operating system
// font size.
void ImGuiBackend_Init(GLFWwindow *window, bool legacyGl, int themeIndex, int uiFontPt);
// Rebuilds the font atlas at the configured size.
void ImGuiBackend_SetUIFont(GLFWwindow *window, int uiFontPt);
void ImGuiBackend_NewFrame();
void ImGuiBackend_Render();
void ImGuiBackend_Shutdown();
// Switches IniFilename to workspace/.ofxrawhost-layout.ini and loads it if present.
// Returns true if a layout file was loaded (caller should not force DockBuilder).
bool ImGuiBackend_SetWorkspaceIni(const std::string &workspaceDir);
