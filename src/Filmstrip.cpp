#include "Filmstrip.h"

#include "imgio/ImageIO.h"
#include "persist/ProjectPersist.h"

#if defined(__APPLE__)
#include <pthread.h>
#endif

#include <climits>
#include <filesystem>

namespace fs = std::filesystem;

constexpr int kMaxFilmstripTextures = 64;
constexpr int kFilmstripUploadsPerFrame = 2;

void freeFilmstripTextures(Filmstrip &fs) { fs.entries.clear(); }

static void releaseFilmstripTex(FilmstripEntry &e) { e.tex.destroy(); }

void invalidateFilmstripThumbs(Filmstrip &fs) {
  // Keep existing textures on screen; they reload at the new edge and swap in
  // place when the fresh thumbnail arrives (no blank/flicker in between).
  for (FilmstripEntry &e : fs.entries) {
    e.thumbPending = true;
    e.thumbFailed = false;
  }
}

static int countFilmstripTextures(const Filmstrip &fs) {
  int n = 0;
  for (const FilmstripEntry &e : fs.entries)
    if (e.tex.id) ++n;
  return n;
}

static void evictFilmstripLru(Filmstrip &fs, int protectA, int protectB) {
  int victim = -1, oldest = INT_MAX;
  for (int i = 0; i < (int)fs.entries.size(); ++i) {
    if (i == protectA || i == protectB) continue;
    const FilmstripEntry &e = fs.entries[i];
    if (!e.tex.id) continue;
    if (e.thumbLru < oldest) {
      oldest = e.thumbLru;
      victim = i;
    }
  }
  if (victim < 0) return;
  FilmstripEntry &e = fs.entries[victim];
  releaseFilmstripTex(e);
  e.thumbPending = true;
  e.thumbLoading = false;
}

int filmstripIndexForPath(const Filmstrip &fs, const std::string &path) {
  auto it = fs.pathIndex.find(path);
  if (it != fs.pathIndex.end()) return it->second;
  // Slow path for a path that differs in form (relative, symlink, trailing slash).
  for (int i = 0; i < (int)fs.entries.size(); ++i) {
    std::error_code ec;
    if (fs::equivalent(fs.entries[i].path, path, ec)) return i;
  }
  return -1;
}

static void finishFilmstripThumbJob(Filmstrip &fs, const std::string &path, bool failed) {
  const int i = filmstripIndexForPath(fs, path);
  if (i < 0) return;
  FilmstripEntry &e = fs.entries[i];
  e.thumbLoading = false;
  if (failed) {
    e.thumbFailed = true;
    e.thumbPending = false;
  }
}

void requestFilmstripThumb(Filmstrip &fs, int index, bool front) {
  if (index < 0 || index >= (int)fs.entries.size()) return;
  FilmstripEntry &e = fs.entries[index];
  if (e.thumbFailed || e.thumbLoading || !e.thumbPending) return;
  const std::string path = e.path;
  {
    std::lock_guard<std::mutex> lock(fs.mutex);
    if (fs.queued.count(path)) return;
    if (front)
      fs.queue.push_front(path);
    else
      fs.queue.push_back(path);
    fs.queued.insert(path);
  }
  e.thumbLoading = true;
  fs.cv.notify_one();
}

static void uploadFilmstripThumbData(Filmstrip &fs, int index, const std::vector<unsigned char> &rgba, int w, int h) {
  if (index < 0 || index >= (int)fs.entries.size() || w <= 0 || h <= 0) return;
  while (countFilmstripTextures(fs) >= kMaxFilmstripTextures) evictFilmstripLru(fs, index, fs.index);
  FilmstripEntry &e = fs.entries[index];
  e.tex.upload(rgba.data(), w, h);
  e.thumbPending = false;
  e.thumbLoading = false;
  e.thumbLru = ++fs.lruTick;
}

void pumpFilmstripThumbs(Filmstrip &fs) {
  const int wantEdge = fs.thumbEdge.load();
  for (int n = 0; n < kFilmstripUploadsPerFrame; ++n) {
    ThumbReady ready;
    {
      std::lock_guard<std::mutex> lock(fs.mutex);
      if (fs.ready.empty()) break;
      ready = std::move(fs.ready.front());
      fs.ready.pop_front();
    }
    if (ready.kind == ThumbReady::Kind::Canceled) {
      finishFilmstripThumbJob(fs, ready.path, false);
      continue;
    }
    const int index = filmstripIndexForPath(fs, ready.path);
    if (index < 0) continue;
    if (ready.edge != wantEdge) {
      // Rendered at a stale resolution: drop it and re-queue at the current size.
      finishFilmstripThumbJob(fs, ready.path, false);
      FilmstripEntry &e = fs.entries[index];
      e.thumbFailed = false;
      e.thumbPending = true;
      continue;
    }
    if (ready.kind == ThumbReady::Kind::Fail) {
      finishFilmstripThumbJob(fs, ready.path, true);
      continue;
    }
    uploadFilmstripThumbData(fs, index, ready.rgba, ready.w, ready.h);
  }
}

static void filmstripThumbWorker(Filmstrip *fs, std::atomic<bool> *quit) {
  while (!*quit) {
    std::string path;
    int gen = 0;
    int edge = 0;
    {
      std::unique_lock<std::mutex> lock(fs->mutex);
      fs->cv.wait(lock, [&] { return *quit || !fs->queue.empty(); });
      if (*quit) break;
      path = fs->queue.front();
      fs->queue.pop_front();
      fs->queued.erase(path);
      gen = fs->gen.load();
      edge = fs->thumbEdge.load();
    }
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    const bool ok = loadThumbnailRGBA(path, edge, rgba, w, h);
    std::lock_guard<std::mutex> lock(fs->mutex);
    if (gen != fs->gen.load()) {
      fs->ready.push_back({path, {}, 0, 0, edge, ThumbReady::Kind::Canceled});
      continue;
    }
    if (!ok)
      fs->ready.push_back({path, {}, 0, 0, edge, ThumbReady::Kind::Fail});
    else
      fs->ready.push_back({path, std::move(rgba), w, h, edge, ThumbReady::Kind::Ok});
  }
}

#if defined(__APPLE__)
std::thread startFilmstripThumbThread(Filmstrip *fs, std::atomic<bool> *quit) {
  struct Ctx {
    Filmstrip *fs;
    std::atomic<bool> *quit;
  };
  auto *ctx = new Ctx{fs, quit};
  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
  const int err = pthread_create(
      &tid, &attr,
      +[](void *p) -> void * {
        auto *c = static_cast<Ctx *>(p);
        filmstripThumbWorker(c->fs, c->quit);
        delete c;
        return nullptr;
      },
      ctx);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    delete ctx;
    return std::thread(filmstripThumbWorker, fs, quit);
  }
  return std::thread([tid]() { pthread_join(tid, nullptr); });
}
#else
std::thread startFilmstripThumbThread(Filmstrip *fs, std::atomic<bool> *quit) {
  return std::thread(filmstripThumbWorker, fs, quit);
}
#endif

void refreshFilmstrip(Filmstrip &fs, const std::string &workspaceDir, const std::string &activePath) {
  ++fs.gen;
  {
    std::lock_guard<std::mutex> lock(fs.mutex);
    fs.queue.clear();
    fs.queued.clear();
    fs.ready.clear();
  }
  freeFilmstripTextures(fs);
  fs.pathIndex.clear();
  if (workspaceDir.empty()) {
    fs.index = -1;
    return;
  }
  const auto paths = listWorkspaceImages(workspaceDir);
  fs.entries.reserve(paths.size());
  for (const std::string &p : paths) {
    FilmstripEntry e;
    e.path = p;
    e.isRaw = isRawImageExtension(lowerFileExtension(p));
    fs.entries.push_back(std::move(e));
  }
  for (int i = 0; i < (int)fs.entries.size(); ++i) fs.pathIndex.emplace(fs.entries[i].path, i);
  fs.index = filmstripIndexForPath(fs, activePath);
}
