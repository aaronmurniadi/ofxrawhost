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

// The combo shows these in enum order, so the values double as the persisted
// setting. "Fit to preview" tracks the preview panel size; the fixed entries
// cap the 16:9 long edge. maxEdge 0 keeps the full resolution, -1 means fit.
enum class PreviewRes { Fit = 0, R720p, R1080p, R1440p, Full };

inline constexpr struct {
  const char *label;
  int maxEdge;
} kPreviewRes[] = {
    {"Fit to preview", -1},
    {"720p", 1280},
    {"1080p", 1920},
    {"1440p", 2560},
    {"Full res", 0},
};
inline constexpr int kPreviewResCount = 5;

// Preview long-edge cap used before the panel has reported its size.
inline constexpr int kFitPreviewFallbackMaxEdge = 1920;

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
  int uiFontSizePt = 13;       // UI font size in points, before the Retina scale
  std::string uiFontFamily;    // absolute path to the font file; empty is the system default
  int settingsSection = 0;     // row selected in the settings modal sidebar
  float previewZoom = 1.0f;  // 1 = fit in view
  float previewPanX = 0.0f;
  float previewPanY = 0.0f;
  ExportFormat exportFormat = ExportFormat::JPEG;
  int exportBitDepth = 8;
  int exportQuality = 92;
  bool exportLossless = false;
  PreviewRes previewRes = PreviewRes::Fit;
  // Preview canvas size in framebuffer pixels, republished by the preview panel
  // every frame. "Fit to preview" renders the preview at this size.
  int previewAreaW = 0;
  int previewAreaH = 0;
  double previewFitSettleTime = 0.0;  // ImGui time when the fit size last changed
  char paramFilter[128] = {};
  char pluginFilter[128] = {};
  // Node index the parameter tab bar selected last frame. The tab bar pushes
  // SetSelected only when the node list or the graph changed the selection, so
  // a tab click and the node list do not fight over the visible tab.
  int paramTabSync = -1;
  bool showAbout = false;
  bool showDonate = false;
  bool showExportDialog = false;
  bool showSettings = false;
  bool themeApplyPending = false;
  bool fontApplyPending = false;
  bool layoutApplyPending = false;
  std::string pendingWorkspaceDir;
};

// Long-edge cap for the current preview size. "Fit to preview" follows the
// preview panel size in framebuffer pixels, so 100% zoom maps one image pixel to
// one screen pixel. The fixed entries are their own cap.
inline int previewMaxEdge(const GuiState &gui) {
  const int cap = kPreviewRes[(int)gui.previewRes].maxEdge;
  if (cap >= 0) return cap;
  const int edge = std::max(gui.previewAreaW, gui.previewAreaH);
  if (edge > 0) return edge;
  return kFitPreviewFallbackMaxEdge;
}

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
