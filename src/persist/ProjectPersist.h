#pragma once

#include <map>
#include <string>
#include <vector>

enum class ColorSpace;

struct PersistGui {
  int outputIndex = 0;
  int exportFormat = 1;
  int jpegQuality = 92;
  int previewRes = 1;
  int themeIndex = 2;
  bool showLeft = true;
  bool showRight = true;
  float leftW = 280.0f;
  float rightW = 420.0f;
  bool showFilmstrip = true;
  float filmstripH = 96.0f;
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
  std::string inputColorSpace;
  std::string exportedAt;
  PersistGui gui;
  PersistChain chain;
};

std::string workspaceProjectPath(const std::string &workspaceDir);
std::string inputSidecarPath(const std::string &imagePath);
std::string exportSidecarPath(const std::string &exportPath);

bool isSupportedImagePath(const std::string &path);
bool isHostMetadataPath(const std::string &path);
std::vector<std::string> openImageDialogFilters();
std::vector<std::string> listWorkspaceImages(const std::string &workspaceDir);

bool loadWorkspaceProject(const std::string &workspaceDir, PersistGui &gui, std::string &activeImageRel);
bool saveWorkspaceProject(const std::string &workspaceDir, const PersistGui &gui, const std::string &activeImageRel);

bool loadSidecarFile(const std::string &path, PersistSidecar &out);
bool saveInputSidecar(const std::string &imagePath, ColorSpace inputSpace, const PersistGui &gui, const PersistChain &chain);
bool saveExportSidecar(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace inputSpace,
                       const PersistGui &gui, const PersistChain &chain);

std::string relativeToWorkspace(const std::string &workspaceDir, const std::string &absPath);
