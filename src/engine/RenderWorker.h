// The render worker: owns its thread, its schedule, and the display buffers, and
// exposes the operations the interface needs (request, recolor, rebuild, upload).
//
// The worker no longer reads App fields while the UI writes them: everything it
// needs comes from RenderHost, and the caller keeps the "waitIdle before mutating
// the chain" contract that the non-copyable Effect instances force.

#pragma once

#include "engine/RenderHost.h"
#include "engine/RenderSchedule.h"
#include "gfx/GlTexture.h"

#include <mutex>
#include <thread>
#include <vector>

class RenderWorker {
 public:
  // The host must outlive the worker.
  explicit RenderWorker(RenderHost &host) : host_(&host) {}
  ~RenderWorker();
  RenderWorker(const RenderWorker &) = delete;
  RenderWorker &operator=(const RenderWorker &) = delete;

  // Spawns the worker thread. The requests below work without it, so a caller
  // that only drives the commands (a test) needs no thread.
  void start();
  // Cancels queued work, then joins.
  void stop();

  // Drops queued work and waits until no render, publish, or export is active.
  void waitIdle();
  // Renders the current host state. Falls back to showing the source when the
  // chain is empty.
  void request();
  // Re-encodes the cached display frame into the preview space, without a render.
  void recolor();
  // Recomputes the preview image from the full one at the wanted cap, then renders.
  void rebuildPreview();
  // Uploads a finished frame to the texture. Call on the UI thread.
  void pumpUpload(GlTexture &texture);

  // Shared with the export worker, which keeps its own quiet window in here.
  RenderSchedule &schedule() { return schedule_; }

  // The latest rendered frame, copied under the display lock.
  Image displayCopy() const;

  // Long-edge cap of the preview the worker is currently fed.
  int builtPreviewMaxEdge() const { return builtPreviewMaxEdge_; }

 private:
  void run();
  void publish(Image img, ColorSpace from, ColorSpace to);
  void showSourcePreview();

  RenderSchedule schedule_;
  RenderHost *host_ = nullptr;
  std::thread thread_;

  mutable std::mutex displayMutex_;
  Image display_;                        // latest rendered (bottom-up float)
  std::vector<unsigned char> displayRGBA_;  // top-down 8-bit, ready for GL upload
  bool displayDirty_ = false;

  // Long-edge cap of the preview the worker is fed. The UI compares it against
  // the wanted cap to rebuild the preview when "Fit to preview" moves.
  int builtPreviewMaxEdge_ = -1;
};
