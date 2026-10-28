// Request types at the interface boundary. A panel fills one of these, and the
// manager validates it before touching state or the disk.
#pragma once

#include <string>

// A destination the user picked in the save dialog. An empty extension means the
// manager appends the one that matches the current export format.
struct ExportRequest {
  std::string outPath;
};

// A destination the user picked to open. Empty means there is nothing to open.
struct OpenRequest {
  std::string path;
};

// A directory the user picked to become the workspace.
struct OpenWorkspaceRequest {
  std::string dir;
};

// One color-space or preview-resolution choice from a panel. The manager maps the
// target to the state it belongs to and clamps the index, so a panel never
// decides which field moves.
struct ColorSpaceChange {
  enum class Target { Input, Preview, Export, ExportIntent, PreviewResolution };

  Target target = Target::Input;
  int index = 0;
};
