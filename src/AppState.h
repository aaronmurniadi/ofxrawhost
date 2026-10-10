#pragma once

#include "Chain.h"
#include "Document.h"
#include "Filmstrip.h"
#include "RenderState.h"
#include "ui/GlTexture.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>

// Forward-declared so this header does not pull in GLFW/OpenGL.
struct GLFWwindow;

inline constexpr const char *kOutputSpaces[] = {"sRGB", "Display P3", "Linear Rec.709", "Linear Rec.2020"};
inline constexpr int kOutputSpaceCount = 4;

enum class ExportFormat { PNG = 0, JPEG = 1, TIFF = 2, WEBP = 3, JXL = 4 };
inline constexpr int kExportFormatCount = 5;

// Per-format export settings. A format ignores settings it cannot honor, and the
// export dialog hides them, so both stay in step with these three predicates.
inline bool exportUsesBitDepth(ExportFormat fmt) {
  return fmt == ExportFormat::PNG || fmt == ExportFormat::TIFF || fmt == ExportFormat::JXL;
}
inline bool exportUsesQuality(ExportFormat fmt) {
  return fmt == ExportFormat::JPEG || fmt == ExportFormat::WEBP || fmt == ExportFormat::JXL;
}
inline bool exportSupportsLossless(ExportFormat fmt) {
  return fmt == ExportFormat::WEBP || fmt == ExportFormat::JXL;
}

enum class PreviewRes { R720p = 0, R1080p, R1440p, Full };

// Long-edge caps for 16:9 frames; 0 = no downscale.
inline constexpr struct {
  const char *label;
  int maxEdge;
} kPreviewRes[] = {
    {"720p", 1280},
    {"1080p", 1920},
    {"1440p", 2560},
    {"Full res", 0},
};
inline constexpr int kPreviewResCount = 4;

inline ColorSpace outputSpace(int index) {
  index = std::clamp(index, 0, 3);
  return static_cast<ColorSpace>(index);
}

// Working buffers are scene-linear (stbi_loadf / LibRaw). Gamma tags (sRGB, Display P3)
// describe the *file*; for CMS display of unprocessed source use the linear counterpart.
inline ColorSpace linearWorkingSpace(ColorSpace fileOrTag) {
  switch (fileOrTag) {
    case ColorSpace::sRGB:
      return ColorSpace::LinearRec709;
    case ColorSpace::DisplayP3:
      // No linear-P3 tag yet; Rec.2020 is the closest wider linear space we have.
      return ColorSpace::LinearRec2020;
    case ColorSpace::LinearRec709:
    case ColorSpace::LinearRec2020:
      return fileOrTag;
  }
  return ColorSpace::LinearRec709;
}

// UI-only state: layout, theme, view transform, persistable preferences.
struct GuiState {
  bool showLeft = true;
  bool showRight = true;
  bool showFilmstrip = false;
  int themeIndex = 2;  // Photoshop
  float uiFontSizePt = 13.0f;  // logical UI font size (before Retina scale)
  float previewZoom = 1.0f;  // 1 = fit in view
  float previewPanX = 0.0f;
  float previewPanY = 0.0f;
  ExportFormat exportFormat = ExportFormat::JPEG;
  int exportBitDepth = 8;
  int exportQuality = 92;
  bool exportLossless = false;
  PreviewRes previewRes = PreviewRes::R1080p;
  char paramFilter[128] = {};
  char pluginFilter[128] = {};
  bool showAbout = false;
  bool showDonate = false;
  bool showExportDialog = false;
  bool themeApplyPending = false;
  bool layoutApplyPending = false;
  std::string pendingWorkspaceDir;
};

// Composition root: owns the window and the cohesive state units. Domain modules
// take the narrow unit they need (DocumentState&, ChainState&, RenderState&,
// Filmstrip&) rather than this whole struct.
struct App {
  GLFWwindow *window = nullptr;
  GlTexture tex;
  DocumentState doc;
  GuiState gui;
  ChainState chain;
  RenderState render;
  Filmstrip filmstrip;

  std::atomic<ColorSpace> outputTag{ColorSpace::sRGB};  // written on the UI thread while renders are in flight
  std::atomic<bool> quit{false};                        // app lifetime; stops both workers

  std::mutex statusMutex;
  std::string status = "Open an image. Source is fed to the plugin as scene-linear.";
  void setStatus(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status = s;
  }
  std::string getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
  }
};
