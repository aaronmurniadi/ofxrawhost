#pragma once

#include "gfx/GlTexture.h"
#include "services/Workspace.h"

// Forward-declared so this header does not pull in GLFW/OpenGL.
struct GLFWwindow;
class DocumentManager;
class ExportManager;
class ColorSpaceManager;
class NodeGraphManager;
class ParamEditManager;

// Composition root: the running host's state plus the window and the preview
// texture, which only the interface draws. Commands and services take a
// ManagerSession, so they cannot reach either one. Manager pointers are set by
// RunApp from Bootstrap; they are non-owning.
struct App : Workspace {
  GLFWwindow *window = nullptr;
  GlTexture previewTexture;
  DocumentManager *documents = nullptr;
  ExportManager *exports = nullptr;
  ColorSpaceManager *colorSpaces = nullptr;
  NodeGraphManager *nodeGraph = nullptr;
  ParamEditManager *paramEdits = nullptr;
};
