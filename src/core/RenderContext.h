// The coordination primitives a render pass shares with the UI thread.
//
// Three loose process globals used to carry this: an abort counter bumped to
// cancel in-flight work, a mutex around parameter values, and a message callback.
// They are one named object now, so the operations are typed (beginPass, cancel,
// isCurrent) instead of "increment an int and hope".
//
// There is exactly one instance for the process because the OpenFX host handle
// is process-wide too: plugins are discovered once and every Effect belongs to
// that host. A second render context would describe a second host, which this
// design does not have.

#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class RenderContext {
 public:
  // Starts a pass and returns its generation. Pass it to isCurrent() to find out
  // whether a newer request has superseded this one.
  int beginPass();
  // Invalidates every pass in flight. Called when the input, the chain, or the
  // output settings change, so a stale frame is never published.
  void cancelInFlight();
  bool isCurrent(int generation) const;
  // The generation counter itself, for the plugins that compare it directly.
  int generation() const;

  // Guards Param values, which an in-flight render reads while the UI writes.
  std::mutex &values();

  // Where plugin messages go. Installed once by the composition root.
  void setMessageSink(std::function<void(const std::string &)> sink);
  // Replaces the sink and returns the previous one, for a caller that reports
  // messages only for the duration of its scope.
  std::function<void(const std::string &)> swapMessageSink(std::function<void(const std::string &)> sink);
  void postMessage(const std::string &text);

 private:
  std::atomic<int> generation_{0};
  std::mutex values_;
  std::mutex sinkMutex_;
  std::function<void(const std::string &)> sink_;
};

RenderContext &renderContext();
