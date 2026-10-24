// The export worker: one full-resolution render off the UI thread.
//
// It borrows the chain (an Effect instance is not copyable) and holds the render
// schedule for its quiet window, so the preview worker stands down while it runs.
// The result comes back through a completion callback, so the caller decides
// where a message goes.

#pragma once

#include "engine/ExportJob.h"
#include "engine/RenderSchedule.h"
#include "model/Chain.h"

#include <atomic>
#include <functional>
#include <thread>

class ExportWorker {
 public:
  using Completion = std::function<void(const ExportResult &result)>;

  ExportWorker() = default;
  ~ExportWorker();
  ExportWorker(const ExportWorker &) = delete;
  ExportWorker &operator=(const ExportWorker &) = delete;

  // Returns false when an export is already in flight, so a second request is
  // refused instead of queued.
  bool start(ChainState &chain, ExportJob job, RenderSchedule &schedule, Completion done);
  // Joins a finished export. Safe to call at any time.
  void wait();
  bool busy() const { return busy_.load(); }

 private:
  std::thread thread_;
  std::atomic<bool> busy_{false};
};
