#include "services/OpenWorker.h"

#include "codecs/ImageIO.h"

#include <functional>
#include <utility>

#if defined(__APPLE__)
#include <pthread.h>
#endif

namespace {

// RAW decode touches LibRaw, whose own state is about 750 KB, so the decode
// thread gets the same 8 MB stack the filmstrip thumbnail thread uses. The
// default 512 KB worker stack is too small for it.
std::thread startLargeStackThread(std::function<void()> work) {
#if defined(__APPLE__)
  auto *ctx = new std::function<void()>(std::move(work));
  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
  const int err = pthread_create(
      &tid, &attr,
      +[](void *p) -> void * {
        auto *task = static_cast<std::function<void()> *>(p);
        (*task)();
        delete task;
        return nullptr;
      },
      ctx);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    std::function<void()> task = std::move(*ctx);
    delete ctx;
    return std::thread(std::move(task));
  }
  return std::thread([tid]() { pthread_join(tid, nullptr); });
#else
  return std::thread(std::move(work));
#endif
}

}  // namespace

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
  thread_ = startLargeStackThread([this, moved = std::move(path), useSavedChain]() mutable {
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
