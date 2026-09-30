#pragma once

#include "imgio/ImageIO.h"
#include "ofx/OfxHost.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

struct Node {
  int pluginIndex = -1;
  bool enabled = true;
  std::unique_ptr<Effect> instance;
  std::map<std::string, bool> groupOpen;
};

struct ThumbReady {
  std::string path;
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  int edge = 0;  // long-edge cap the job was rendered at
  enum class Kind { Ok, Fail, Canceled } kind = Kind::Fail;
};

struct FilmstripEntry {
  std::string path;
  unsigned int tex = 0;
  int tw = 0, th = 0;
  bool thumbPending = true;
  bool thumbLoading = false;
  bool thumbFailed = false;
  int thumbLru = 0;
};

inline constexpr const char *kOutputSpaces[] = {"sRGB", "Display P3", "Linear Rec.709", "Linear Rec.2020"};
inline constexpr int kOutputSpaceCount = 4;

enum class ExportFormat { PNG = 0, JPEG = 1 };

enum class PreviewRes { R720p = 0, R1080p, R1440p, Full };

enum class FilmstripTab { All = 0, RAW, Compressed };

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

struct App {
  GLFWwindow *window = nullptr;
  unsigned int tex = 0;
  int texW = 0, texH = 0;

  Image full, preview;
  std::string path, status = "Open an image. Source is fed to the plugin as scene-linear.";
  ColorSpace inputSpace = ColorSpace::LinearRec2020;
  std::atomic<ColorSpace> outputTag{ColorSpace::sRGB};  // written on the UI thread while renders are in flight
  ExportFormat exportFormat = ExportFormat::JPEG;
  int jpegQuality = 92;
  PreviewRes previewRes = PreviewRes::R1080p;
  bool showLeft = true;
  bool showRight = true;
  // Legacy layout sizes (read from old JSON; DockBuilder uses them once if no .ini).
  float leftW = 280.0f;
  float rightW = 420.0f;
  char paramFilter[128] = {};
  char pluginFilter[128] = {};
  float previewZoom = 1.0f;  // 1 = fit in view
  int themeIndex = 2;        // Photoshop
  float previewPanX = 0.0f;
  float previewPanY = 0.0f;
  std::vector<Node> nodes;
  int selectedNode = -1;

  std::string workspaceDir;
  std::vector<FilmstripEntry> filmstrip;
  int filmstripIndex = -1;
  bool showFilmstrip = true;
  float filmstripH = 96.0f;
  FilmstripTab filmstripTab = FilmstripTab::All;
  bool showAbout = false;
  bool showDonate = false;

  std::atomic<int> filmstripGen{0};
  std::atomic<int> filmstripThumbEdge{256};  // snapped long-edge cap (see kFilmstripThumbEdges)
  int thumbLruTick = 0;
  std::thread thumbThread;
  std::mutex thumbMutex;
  std::condition_variable thumbCv;
  std::deque<std::string> thumbQueue;
  std::unordered_set<std::string> thumbQueued;
  std::deque<ThumbReady> thumbReady;

  std::string pendingWorkspaceDir;
  bool themeApplyPending = false;
  bool layoutApplyPending = false;

  std::mutex renderMutex;
  std::condition_variable renderCv;
  std::atomic<bool> quit{false};
  std::atomic<bool> renderPending{false};
  bool renderBusy = false;  // guarded by renderMutex: worker is in its render/publish section
  bool exportBusy = false;  // guarded by renderMutex: a full-res export thread is running
  std::thread renderThread;
  Image display;  // latest rendered (bottom-up float), guarded by displayMutex
  std::vector<unsigned char> displayRGBA;  // sRGB8 top-down, ready for GL upload
  std::mutex displayMutex;
  bool displayDirty = false;
  bool displayRecolorPending = false;

  std::mutex statusMutex;
  void setStatus(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status = s;
  }
  std::string getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
  }
};
