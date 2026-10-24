// The message, memory and multithread suites.
//
// These three are what a plugin uses to talk to the user, to allocate scratch
// memory, and to spread a render over the pool. They share no state with the
// parameter and clip suites, so they live on their own.

#include "ofx/OfxSuitesPriv.h"

#include "core/RenderContext.h"
#include "ofx/OfxProps.h"

#include "ofxMessage.h"
#include "ofxMultiThread.h"

#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// Message, memory, multithread suites

static OfxStatus message(void *, const char *type, const char *, const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  const char *format = "";
  if (fmt) format = fmt;
  vsnprintf(buf, sizeof buf, format, ap);
  va_end(ap);
  if (!type) type = "";
  fprintf(stderr, "OFX %s: %s\n", type, buf);
  // The status bar shows problems and one-shot reports. Log lines stay on
  // stderr, because a render can raise many of them.
  if (!strcmp(type, kOfxMessageError) || !strcmp(type, kOfxMessageFatal) || !strcmp(type, kOfxMessageWarning) ||
      !strcmp(type, kOfxMessageMessage)) {
    if (buf[0]) renderContext().postMessage(buf);
    else renderContext().postMessage("(plugin message)");
  }
  if (!strcmp(type, kOfxMessageQuestion)) return kOfxStatReplyYes;
  return kOfxStatOK;
}
static OfxStatus memoryAlloc(void *, size_t n, void **out) {
  *out = malloc(n);
  if (*out) return kOfxStatOK;
  return kOfxStatErrMemory;
}
static OfxStatus memoryFree(void *p) {
  free(p);
  return kOfxStatOK;
}

static thread_local unsigned tIndex = 0;
static thread_local bool tSpawned = false;
static unsigned cpuCount() { return std::max(1u, std::thread::hardware_concurrency()); }

static std::mutex gMtMu;
static std::condition_variable gMtCv;
static std::condition_variable gMtDone;
static OfxThreadFunctionV1 *gMtF = nullptr;
static void *gMtArg = nullptr;
static unsigned gMtN = 0;
static std::atomic<unsigned> gMtNext{0};
static unsigned gMtWorkersDone = 0;
static unsigned gMtParticipants = 0;
static std::atomic<bool> gMtActive{false};
static std::atomic<bool> gMtShutdown{false};
static std::vector<std::thread> gMtPool;

static void mtRunSlices() {
  for (;;) {
    const unsigned i = gMtNext.fetch_add(1, std::memory_order_relaxed);
    if (i >= gMtN) break;
    tIndex = i;
    tSpawned = true;
    gMtF(i, gMtN, gMtArg);
  }
}

static void mtWorkerLoop() {
  for (;;) {
    std::unique_lock<std::mutex> lock(gMtMu);
    gMtCv.wait(lock, [] { return gMtShutdown.load() || gMtActive.load(); });
    if (gMtShutdown.load()) return;
    lock.unlock();
    mtRunSlices();
    lock.lock();
    if (++gMtWorkersDone >= gMtParticipants) gMtDone.notify_one();
  }
}

static void mtEnsurePool() {
  static std::once_flag once;
  std::call_once(once, [] {
    const unsigned nw = cpuCount();
    for (unsigned i = 1; i < nw; ++i) gMtPool.emplace_back(mtWorkerLoop);
  });
}

static OfxStatus multiThreadEphemeral(OfxThreadFunctionV1 f, unsigned n, void *arg) {
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < n; ++i)
    threads.emplace_back([=] {
      tIndex = i;
      tSpawned = true;
      f(i, n, arg);
    });
  for (auto &t : threads) t.join();
  return kOfxStatOK;
}

static OfxStatus multiThread(OfxThreadFunctionV1 f, unsigned n, void *arg) {
  if (!f) return kOfxStatFailed;
  if (n <= 1) {
    tIndex = 0;
    tSpawned = false;
    f(0, 1, arg);
    return kOfxStatOK;
  }
  if (gMtActive.load() || tSpawned) return multiThreadEphemeral(f, n, arg);
  mtEnsurePool();
  const unsigned helpers = std::min((unsigned)gMtPool.size(), n - 1);
  {
    std::unique_lock<std::mutex> lock(gMtMu);
    gMtF = f;
    gMtArg = arg;
    gMtN = n;
    gMtNext.store(0, std::memory_order_relaxed);
    gMtWorkersDone = 0;
    gMtParticipants = 1 + helpers;
    gMtActive.store(true);
  }
  gMtCv.notify_all();
  tSpawned = false;
  mtRunSlices();
  {
    std::unique_lock<std::mutex> lock(gMtMu);
    if (++gMtWorkersDone >= gMtParticipants) gMtDone.notify_one();
    gMtDone.wait(lock, [] { return gMtWorkersDone >= gMtParticipants; });
    gMtActive.store(false);
  }
  return kOfxStatOK;
}
static OfxStatus multiThreadNumCPUs(unsigned *n) {
  *n = cpuCount();
  return kOfxStatOK;
}
static OfxStatus multiThreadIndex(unsigned *i) {
  *i = tIndex;
  return kOfxStatOK;
}
static int multiThreadIsSpawnedThread() { return tSpawned; }
static std::recursive_mutex *MX(OfxMutexHandle m) { return reinterpret_cast<std::recursive_mutex *>(m); }
static OfxStatus mutexCreate(OfxMutexHandle *m, int lockCount) {
  auto *mx = new std::recursive_mutex;
  for (int i = 0; i < lockCount; ++i) mx->lock();
  *m = reinterpret_cast<OfxMutexHandle>(mx);
  return kOfxStatOK;
}
static OfxStatus mutexDestroy(const OfxMutexHandle m) {
  delete MX(m);
  return kOfxStatOK;
}
static OfxStatus mutexLock(const OfxMutexHandle m) {
  MX(m)->lock();
  return kOfxStatOK;
}
static OfxStatus mutexUnLock(const OfxMutexHandle m) {
  MX(m)->unlock();
  return kOfxStatOK;
}
static OfxStatus mutexTryLock(const OfxMutexHandle m) {
  if (MX(m)->try_lock()) return kOfxStatOK;
  return kOfxStatFailed;
}


static const OfxMessageSuiteV1 gMessageSuite = [] {
  OfxMessageSuiteV1 s{};
  s.message = message;
  return s;
}();
static const OfxMemorySuiteV1 gMemorySuite = [] {
  OfxMemorySuiteV1 s{};
  s.memoryAlloc = memoryAlloc;
  s.memoryFree = memoryFree;
  return s;
}();
static const OfxMultiThreadSuiteV1 gThreadSuite = [] {
  OfxMultiThreadSuiteV1 s{};
  s.multiThread = multiThread;
  s.multiThreadNumCPUs = multiThreadNumCPUs;
  s.multiThreadIndex = multiThreadIndex;
  s.multiThreadIsSpawnedThread = multiThreadIsSpawnedThread;
  s.mutexCreate = mutexCreate;
  s.mutexDestroy = mutexDestroy;
  s.mutexLock = mutexLock;
  s.mutexUnLock = mutexUnLock;
  s.mutexTryLock = mutexTryLock;
  return s;
}();

const OfxMessageSuiteV1 &messageSuiteTable() { return gMessageSuite; }
const OfxMemorySuiteV1 &memorySuiteTable() { return gMemorySuite; }
const OfxMultiThreadSuiteV1 &threadSuiteTable() { return gThreadSuite; }
