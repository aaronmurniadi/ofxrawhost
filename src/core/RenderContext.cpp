#include "core/RenderContext.h"

#include <utility>

int RenderContext::beginPass() { return ++generation_; }

void RenderContext::cancelInFlight() { ++generation_; }

bool RenderContext::isCurrent(int generation) const { return generation == generation_.load(); }

int RenderContext::generation() const { return generation_.load(); }

std::mutex &RenderContext::values() { return values_; }

void RenderContext::setMessageSink(std::function<void(const std::string &)> sink) {
  std::lock_guard<std::mutex> lock(sinkMutex_);
  sink_ = std::move(sink);
}

std::function<void(const std::string &)> RenderContext::swapMessageSink(std::function<void(const std::string &)> sink) {
  std::lock_guard<std::mutex> lock(sinkMutex_);
  std::function<void(const std::string &)> previous = std::move(sink_);
  sink_ = std::move(sink);
  return previous;
}

void RenderContext::postMessage(const std::string &text) {
  std::function<void(const std::string &)> sink;
  {
    std::lock_guard<std::mutex> lock(sinkMutex_);
    sink = sink_;
  }
  // Called without the lock: the sink reaches the UI, which may post again.
  if (sink) sink(text);
}

RenderContext &renderContext() {
  static RenderContext context;
  return context;
}
