#pragma once

#include <string>

struct GLFWwindow;

void ImGuiBackend_Init(GLFWwindow *window, int themeIndex);
void ImGuiBackend_NewFrame();
void ImGuiBackend_Render();
void ImGuiBackend_Shutdown();
// Switches IniFilename to workspace/.ofxrawhost-layout.ini and loads it if present.
// Returns true if a layout file was loaded (caller should not force DockBuilder).
bool ImGuiBackend_SetWorkspaceIni(const std::string &workspaceDir);
