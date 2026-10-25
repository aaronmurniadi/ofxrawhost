// The render worker: owns its thread, its schedule, its stop flag, the colour
// space the chain writes, and the display buffers.
//
// It knows nothing about the application. A request carries a RenderInputs value
// with the facts the pass needs, and status goes to a StatusSink. The only rule
// a caller keeps is the one the non-copyable Effect instances force: wait for
// idle before mutating the chain or the image a request borrows.

#pragma once

#include "core/StatusSink.h"
#include "engine/RenderInputs.h"
#include "engine/RenderSchedule.h"
#include "gfx/GlTexture.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class RenderWorker {
 public:
  // The sink must outlive the worker.
  explicit RenderWorker(StatusSink &status) : status_(&status) {}
  ~RenderWorker();
  RenderWorker(const RenderWorker &) = delete;
  RenderWorker &operator=(const RenderWorker &) = delete;

  // Spawns the worker thread. The requests below work without it, so a caller
  // that only drives the commands (a test) needs no thread.
  void start();
  // Raises the stop flag, cancels queued work, then joins.
  void stop();
  bool stopping() const { return stopping_.load(); }

  // Drops queued work and waits until no render or export is active.
  void waitIdle();
  // Renders the state described by inputs. Falls back to showing the source when
  // the chain is empty.
  void request(const RenderInputs &inputs);
  // Re-encodes the cached display frame into the preview space, without a render.
  void recolor(const RenderInputs &inputs);
  // Recomputes the preview image from the full one at the wanted cap, then renders.
  void rebuildPreview(const RenderInputs &inputs);
  // Uploads a finished frame to the texture. Call on the UI thread.
  void pumpUpload(GlTexture &texture);

  // Colour space the chain writes, derived by walking the chain.
  void setOutputSpace(ColorSpace space) { outputSpace_ = space; }
  ColorSpace outputSpace() const { return outputSpace_.load(); }

  // Shared with the export worker, which keeps its own quiet window in here.
  RenderSchedule &schedule() { return schedule_; }

  // The latest rendered frame, copied under the display lock.
  Image displayCopy() const;

  // Long-edge cap of the preview the worker is currently fed.
  int builtPreviewMaxEdge() const { return builtPreviewMaxEdge_; }

 private:
  void run();
  void publish(Image img, ColorSpace from, ColorSpace to);
  void showSourcePreview(const RenderInputs &inputs);

  RenderSchedule schedule_;
  RenderInputs inputs_;
  StatusSink *status_ = nullptr;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::atomic<ColorSpace> outputSpace_{ColorSpace::sRGB};

  mutable std::mutex displayMutex_;
  Image display_;                           // latest rendered (bottom-up float)
  std::vector<unsigned char> displayRGBA_;  // top-down 8-bit, ready for GL upload
  bool displayDirty_ = false;

  // Long-edge cap of the preview the worker is fed. The interface compares it
  // against the wanted cap to rebuild the preview when "Fit to preview" moves.
  int builtPreviewMaxEdge_ = -1;
};
