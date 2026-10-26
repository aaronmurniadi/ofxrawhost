#include "services/OpenWorker.h"

#include "codecs/ImageIO.h"

#include <utility>

OpenWorker::~OpenWorker() { wait(); }

void OpenWorker::run(std::string path, bool useSavedChain) {
  DecodedImage decoded;
  decoded.path = path;
  decoded.useSavedChain = useSavedChain;
  decoded.ok = loadImage(path, decoded.full, decoded.fileSpace, decoded.workingSpace, &decoded.meta);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = std::move(decoded);
    done_ = true;
  }
  busy_ = false;
}

bool OpenWorker::start(std::string path, bool useSavedChain) {
  if (busy_ || done_) return false;
  if (thread_.joinable()) thread_.join();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    result_ = DecodedImage{};
  }
  done_ = false;
  busy_ = true;
  thread_ = std::thread([this, moved = std::move(path), useSavedChain]() mutable {
    run(std::move(moved), useSavedChain);
  });
  return true;
}

std::string OpenWorker::path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return path_;
}

bool OpenWorker::take(DecodedImage &out) {
  if (!done_) return false;
  // The thread has finished; join it without the lock so a concurrent path()
  // cannot block behind the join.
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  if (!done_) return false;
  done_ = false;
  out = std::move(result_);
  result_ = DecodedImage{};
  path_.clear();
  return true;
}

void OpenWorker::wait() {
  if (thread_.joinable()) thread_.join();
  busy_ = false;
  done_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  path_.clear();
  result_ = DecodedImage{};
}
