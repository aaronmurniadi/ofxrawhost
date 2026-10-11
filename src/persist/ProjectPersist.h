#pragma once

#include <map>
#include <string>
#include <vector>

enum class ColorSpace;

struct PersistGui {
  int outputIndex = 0;  // preview color space
  int exportIndex = 0;  // export color space
  int exportIntent = 0;  // RenderIntent
  int exportFormat = 1;
  int exportBitDepth = 8;
  int exportQuality = 92;
  bool exportLossless = false;
  int previewRes = 0;
  bool showPlugins = true;
  bool showParameters = true;
  bool showFilmstrip = false;
};

// Preferences that belong to the user and the machine, not to the file or the
// directory that is open. They live in the per-user settings file alone, so a
// project or a sidecar never changes the theme or the interface font.
struct PersistAppearance {
  int themeIndex = 2;  // Photoshop
  int uiFontPt = 0;    // interface font size in points; 0 follows the system font size
};

// Update-check preferences. They live in their own object of the per-user
// settings file, because a workspace project or a sidecar must never turn the
// check on or off for the user.
struct PersistUpdates {
  int channel = 0;  // UpdateChannel: Stable Release, Pre-releases
  bool checkAtStartup = true;
  bool devWarningShown = false;
};

struct PersistNode {
  std::string pluginIdentifier;
  std::string pluginLabel;
  bool enabled = true;
  std::map<std::string, bool> groupOpen;
  // Raw JSON fragment per param value (number, bool, string, or array).
  std::map<std::string, std::string> paramsJson;
};

struct PersistChain {
  int selectedNode = -1;
  std::vector<PersistNode> nodes;
};

struct PersistSidecar {
  std::string kind;
  std::string sourcePath;
  std::string inputColorSpace;      // space the file declares
  std::string chainInputColorSpace;  // space the user picked for the chain
  std::string exportedAt;
  PersistGui gui;
  PersistChain chain;
};

std::string workspaceProjectPath(const std::string &workspaceDir);
// Per-user preferences file, the same PersistGui layout as a workspace project.
// Empty when the user config directory cannot be determined.
std::string globalSettingsPath();
std::string inputSidecarPath(const std::string &imagePath);
std::string exportSidecarPath(const std::string &exportPath);

bool isSupportedImagePath(const std::string &path);
bool isHostMetadataPath(const std::string &path);
std::vector<std::string> openImageDialogFilters();
std::vector<std::string> listWorkspaceImages(const std::string &workspaceDir);

bool loadWorkspaceProject(const std::string &workspaceDir, PersistGui &gui, std::string &activeImageRel);
bool saveWorkspaceProject(const std::string &workspaceDir, const PersistGui &gui, const std::string &activeImageRel);

// Preferences that outlive a file or a directory: the theme, the interface font
// size, panel visibility, preview size, export defaults, and update checking.
// Loaded at start-up before the theme and the font are applied, and saved on
// exit and on every persistWorkspace.
bool loadGlobalSettings(PersistGui &gui, PersistAppearance &appearance, PersistUpdates &updates);
bool saveGlobalSettings(const PersistGui &gui, const PersistAppearance &appearance, const PersistUpdates &updates);

bool loadSidecarFile(const std::string &path, PersistSidecar &out);
bool saveInputSidecar(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
                      const PersistGui &gui, const PersistChain &chain);
bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace fileSpace,
                       const PersistGui &gui, const PersistChain &chain);

std::string relativeToWorkspace(const std::string &workspaceDir, const std::string &absPath);
