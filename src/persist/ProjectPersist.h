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
  int themeIndex = 2;
  int uiFontScale = 1;  // UiFontScale: Smaller, Normal (system default), Bigger
  bool showPlugins = true;
  bool showParameters = true;
  bool showFilmstrip = false;
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

// Preferences that outlive a workspace or an image: theme, font, panel
// visibility, preview size, and export defaults. Loaded at start-up before the
// theme and font are applied, and saved on exit and on every persistWorkspace.
bool loadGlobalSettings(PersistGui &gui);
bool saveGlobalSettings(const PersistGui &gui);

bool loadSidecarFile(const std::string &path, PersistSidecar &out);
bool saveInputSidecar(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
                      const PersistGui &gui, const PersistChain &chain);
bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace fileSpace,
                       const PersistGui &gui, const PersistChain &chain);

std::string relativeToWorkspace(const std::string &workspaceDir, const std::string &absPath);
