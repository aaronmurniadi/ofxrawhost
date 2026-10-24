#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

// Owns the render mutex, the condition variable, and the three request flags that
// the render worker, the export thread, and the UI thread share. Two flags are
// guarded by mutex; pending is atomic so the worker can test it cheaply.
struct RenderSchedule {
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<bool> pending{false};
  bool busy = false;            // guarded by mutex: a render or publish is in flight
  bool exporting = false;       // guarded by mutex: a full-resolution export thread runs
  bool recolorPending = false;  // guarded by mutex: recolor the cached display buffer

  // Drops queued work, then waits until no render and no export is active.
  void waitIdle() {
    std::unique_lock<std::mutex> lock(mutex);
    pending = false;
    recolorPending = false;
    cv.wait(lock, [this] { return !busy && !exporting; });
  }

  // Keeps the worker in its work section, and wakes a waiter on the way out.
  struct Guard {
    explicit Guard(RenderSchedule *schedule, bool exportMode = false) : s(schedule), exportFlag(exportMode) {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (exportFlag) s->exporting = true;
      else s->busy = true;
    }
    ~Guard() {
      std::lock_guard<std::mutex> lock(s->mutex);
      if (exportFlag) s->exporting = false;
      else s->busy = false;
      s->cv.notify_all();
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    RenderSchedule *s;
    bool exportFlag;
  };
};
