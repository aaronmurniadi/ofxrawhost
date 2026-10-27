#pragma once

#include <mutex>
#include <string>
#include <utility>

// The one line of text the interface shows about what just happened, written by
// the render and export threads and read by the panel that draws it.
//
// It lives in core/ because both engine/ and ofx/ report into it and neither may
// link services/. The lock is here rather than at the call sites so a reader
// cannot forget it.
class StatusSink {
 public:
  void report(std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    text_ = std::move(text);
  }

  std::string text() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return text_;
  }

 private:
  mutable std::mutex mutex_;
  std::string text_;
};
