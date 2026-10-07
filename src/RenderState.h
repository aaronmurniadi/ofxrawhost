// Render-thread state owned by App: the schedule, the worker thread, and the
// latest display buffer. Extracted from App so the scheduler and the UI share a
// single named unit instead of loose fields.
#pragma once

#include "RenderSchedule.h"
#include "imgio/ImageIO.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

struct RenderState {
  RenderSchedule schedule;
  std::thread thread;
  // Owned so shutdown can join it; the old detached export thread could outlive App.
  std::thread exportThread;
  std::atomic<bool> exportInFlight{false};
  Image display;  // latest rendered (bottom-up float), guarded by displayMutex
  std::vector<unsigned char> displayRGBA;  // sRGB8 top-down, ready for GL upload
  std::mutex displayMutex;
  bool displayDirty = false;
};
