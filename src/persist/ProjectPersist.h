#pragma once

#include <map>
#include <string>
#include <vector>

enum class ColorSpace;

// The view, preview, and export state of the interface. It belongs to the user,
// not to a file or a directory, so it lives in the per-user settings file alone.
struct PersistGui {
  int outputIndex = 0;  // preview color space
  int exportIndex = 0;  // export color space
  int exportIntent = 0;  // RenderIntent
  int exportFormat = 1;
  int exportBitDepth = 8;
  int exportQuality = 92;
  bool exportLossless = false;
  bool exportSidecar = false;
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
  PersistChain chain;
};

// Paths of the app-level state files. Each returns a path under the per-user
// state directory, and falls back to a file beside the source when that
// directory cannot be determined. The Legacy forms are the files a build before
// the move wrote beside the source, which are still read when the state file is
// missing and are never written again.
std::string workspaceProjectPath(const std::string &workspaceDir);
std::string workspaceProjectPathLegacy(const std::string &workspaceDir);
// Per-user settings file. Empty when the user config directory cannot be
// determined.
std::string globalSettingsPath();
std::string inputSidecarPath(const std::string &imagePath);
std::string inputSidecarPathLegacy(const std::string &imagePath);
std::string exportSidecarPath(const std::string &exportPath);

bool isSupportedImagePath(const std::string &path);
bool isHostMetadataPath(const std::string &path);
std::vector<std::string> openImageDialogFilters();
std::vector<std::string> listWorkspaceImages(const std::string &workspaceDir);

// The directory project records which image was open. The chain of an image
// lives in its sidecar, and the interface state lives in the settings file, so
// neither is here.
bool loadWorkspaceProject(const std::string &workspaceDir, std::string &activeImageRel);
bool saveWorkspaceProject(const std::string &workspaceDir, const std::string &activeImageRel);

// Preferences that outlive a file or a directory: the view, preview, and export
// state of the interface, the theme, the interface font size, and update
// checking. Loaded at start-up before the theme and the font are applied, and
// saved on exit and on every persistWorkspace.
bool loadGlobalSettings(PersistGui &gui, PersistAppearance &appearance, PersistUpdates &updates);
bool saveGlobalSettings(const PersistGui &gui, const PersistAppearance &appearance, const PersistUpdates &updates);

// Reads the sidecar of an image: the state entry under the per-user state
// directory, or the sidecar beside the image that a build before the move wrote.
// A sidecar records the plugin chain and the color spaces and nothing about the
// interface.
bool loadInputSidecar(const std::string &imagePath, PersistSidecar &out);
bool saveInputSidecar(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
                      const PersistChain &chain);
bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace fileSpace,
                       const PersistChain &chain);

std::string relativeToWorkspace(const std::string &workspaceDir, const std::string &absPath);
