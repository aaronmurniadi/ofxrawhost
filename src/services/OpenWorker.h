// Decodes an image on its own thread, so the window keeps drawing while a RAW
// develops. A decode used to run on the interface thread and the whole
// application stopped responding for as long as LibRaw took.
//
// The result is a value with no coupling to application state. The caller takes
// it on the interface thread and applies it there.

#pragma once

#include "core/Image.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct DecodedImage {
  std::string path;
  Image full;
  ColorSpace fileSpace = ColorSpace::LinearRec2020;
  ColorSpace workingSpace = ColorSpace::LinearRec709;
  ImageMeta meta;
  bool ok = false;
  // Which chain the image opens with, decided before the decode started and
  // carried with it so the answer cannot drift while the thread runs.
  bool useSavedChain = true;
};

class OpenWorker {
 public:
  OpenWorker() = default;
  ~OpenWorker();
  OpenWorker(const OpenWorker &) = delete;
  OpenWorker &operator=(const OpenWorker &) = delete;

  // Starts decoding path. False when a decode is already running or its result
  // has not been taken yet, so a second request is refused instead of queued.
  bool start(std::string path, bool useSavedChain);
  bool busy() const { return busy_.load(); }
  // The file being decoded, or empty when idle.
  std::string path() const;
  // Moves out a finished decode. False when nothing is waiting.
  bool take(DecodedImage &out);
  // Joins a running decode and discards whatever it produced.
  void wait();

 private:
  void run(std::string path, bool useSavedChain);

  mutable std::mutex mutex_;
  std::thread thread_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> done_{false};
  std::string path_;
  DecodedImage result_;
};
