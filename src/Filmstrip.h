// Filmstrip thumbnails. The module owns its entries and its worker state, so the
// private queue/mutex/thread no longer live in App.
#pragma once

#include "ui/GlTexture.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ThumbReady {
  std::string path;
  std::vector<unsigned char> rgba;
  int w = 0, h = 0;
  int edge = 0;  // long-edge cap the job was rendered at
  enum class Kind { Ok, Fail, Canceled } kind = Kind::Fail;
};

struct FilmstripEntry {
  std::string path;
  GlTexture tex;
  bool isRaw = false;
  bool thumbPending = true;
  bool thumbLoading = false;
  bool thumbFailed = false;
  int thumbLru = 0;
};

enum class FilmstripTab { All = 0, RAW, Compressed };

// Thumbnail resolutions snap to this ladder as the filmstrip is resized.
constexpr int kFilmstripThumbEdges[] = {16, 24, 32, 48, 256, 1024};

inline int snapFilmstripThumbEdge(float devicePx) {
  for (int edge : kFilmstripThumbEdges)
    if (devicePx <= (float)edge) return edge;
  return kFilmstripThumbEdges[sizeof(kFilmstripThumbEdges) / sizeof(kFilmstripThumbEdges[0]) - 1];
}

struct Filmstrip {
  std::vector<FilmstripEntry> entries;
  std::unordered_map<std::string, int> pathIndex;  // path -> entries index
  int index = -1;
  FilmstripTab tab = FilmstripTab::All;
  std::atomic<int> gen{0};
  std::atomic<int> thumbEdge{256};  // snapped long-edge cap (see kFilmstripThumbEdges)
  int lruTick = 0;
  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::string> queue;
  std::unordered_set<std::string> queued;
  std::deque<ThumbReady> ready;
};

void freeFilmstripTextures(Filmstrip &fs);
int filmstripIndexForPath(const Filmstrip &fs, const std::string &path);
void invalidateFilmstripThumbs(Filmstrip &fs);
void requestFilmstripThumb(Filmstrip &fs, int index, bool front);
void pumpFilmstripThumbs(Filmstrip &fs);
// Rebuilds the entry list for the workspace and selects activePath.
void refreshFilmstrip(Filmstrip &fs, const std::string &workspaceDir, const std::string &activePath);
// quit is the app-lifetime stop flag shared with the render worker.
std::thread startFilmstripThumbThread(Filmstrip *fs, std::atomic<bool> *quit);
