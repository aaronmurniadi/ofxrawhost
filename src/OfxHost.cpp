#include "OfxHost.h"

#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"

#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

static void *loadLib(const fs::path &path) {
#ifdef _WIN32
  return (void *)LoadLibraryW(path.wstring().c_str());
#else
  return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}
static void *sym(void *lib, const char *name) {
#ifdef _WIN32
  return (void *)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}
static const char *loadErr() {
#ifdef _WIN32
  static char buf[64];
  snprintf(buf, sizeof buf, "Win32 error %lu", GetLastError());
  return buf;
#else
  return dlerror();
#endif
}

// ------------------------------------------------------------------ properties

#define CHECK_SET(h, i)                  \
  if (!(h)) return kOfxStatErrBadHandle; \
  if ((i) < 0) return kOfxStatErrBadIndex;

static Val *slot(OfxPropertySetHandle h, const char *k, int i) {
  auto &v = P(h)->m[k];
  if ((int)v.size() <= i) v.resize(i + 1);
  return &v[i];
}
OfxStatus propSetPointer(OfxPropertySetHandle h, const char *k, int i, void *v) {
  CHECK_SET(h, i);
  slot(h, k, i)->p = v;
  return kOfxStatOK;
}
OfxStatus propSetString(OfxPropertySetHandle h, const char *k, int i, const char *v) {
  CHECK_SET(h, i);
  slot(h, k, i)->s = v ? v : "";
  return kOfxStatOK;
}
OfxStatus propSetDouble(OfxPropertySetHandle h, const char *k, int i, double v) {
  CHECK_SET(h, i);
  Val *s = slot(h, k, i);
  s->d = v;
  s->i = std::isfinite(v) ? (int)std::lround(std::clamp(v, (double)INT_MIN, (double)INT_MAX)) : 0;
  return kOfxStatOK;
}
OfxStatus propSetInt(OfxPropertySetHandle h, const char *k, int i, int v) {
  CHECK_SET(h, i);
  Val *s = slot(h, k, i);
  s->i = v;
  s->d = v;
  return kOfxStatOK;
}

static const Val *findVal(OfxPropertySetHandle h, const char *k, int i, OfxStatus &st) {
  st = kOfxStatErrBadHandle;
  if (!h) return nullptr;
  auto it = P(h)->m.find(k);
  st = kOfxStatErrUnknown;
  if (it == P(h)->m.end()) return nullptr;
  st = kOfxStatErrBadIndex;
  if (i < 0 || i >= (int)it->second.size()) return nullptr;
  st = kOfxStatOK;
  return &it->second[i];
}
#define PROP_GETTER(name, T, expr)                                                \
  static OfxStatus name(OfxPropertySetHandle h, const char *k, int i, T *out) {   \
    OfxStatus st;                                                                 \
    if (const Val *v = findVal(h, k, i, st)) *out = expr;                         \
    return st;                                                                    \
  }
PROP_GETTER(propGetPointer, void *, v->p)
PROP_GETTER(propGetString, char *, const_cast<char *>(v->s.c_str()))
PROP_GETTER(propGetDouble, double, v->d)
PROP_GETTER(propGetInt, int, v->i)

template <class T, OfxStatus (*F)(OfxPropertySetHandle, const char *, int, T *)>
static OfxStatus propGetN(OfxPropertySetHandle h, const char *k, int n, T *out) {
  for (int i = 0; i < n; ++i)
    if (OfxStatus st = F(h, k, i, out + i)) return st;
  return kOfxStatOK;
}
static OfxStatus propReset(OfxPropertySetHandle h, const char *k) {
  if (!h) return kOfxStatErrBadHandle;
  P(h)->m.erase(k);
  return kOfxStatOK;
}
static OfxStatus propGetDimension(OfxPropertySetHandle h, const char *k, int *count) {
  if (!h) return kOfxStatErrBadHandle;
  auto it = P(h)->m.find(k);
  if (it == P(h)->m.end()) return kOfxStatErrUnknown;
  *count = (int)it->second.size();
  return kOfxStatOK;
}

std::string sprop(const PropSet &ps, const char *k, int i) {
  auto it = ps.m.find(k);
  return it != ps.m.end() && i < (int)it->second.size() ? it->second[i].s : "";
}
double dprop(const PropSet &ps, const char *k, int i, double fallback) {
  auto it = ps.m.find(k);
  return it != ps.m.end() && i < (int)it->second.size() ? it->second[i].d : fallback;
}

// ------------------------------------------------------ effects, params, clips

std::atomic<int> gLatestGen{0};
std::mutex gValueMutex;

static Effect *E(OfxImageEffectHandle h) { return reinterpret_cast<Effect *>(h); }
static Effect *E(OfxParamSetHandle h) { return reinterpret_cast<Effect *>(h); }
static Param *PA(OfxParamHandle h) { return reinterpret_cast<Param *>(h); }
static Clip *C(OfxImageClipHandle h) { return reinterpret_cast<Clip *>(h); }

int dims(const std::string &t) {
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean || t == kOfxParamTypeChoice) return 1;
  if (t == kOfxParamTypeDouble2D || t == kOfxParamTypeInteger2D) return 2;
  if (t == kOfxParamTypeDouble3D || t == kOfxParamTypeInteger3D || t == kOfxParamTypeRGB) return 3;
  if (t == kOfxParamTypeRGBA) return 4;
  return 0;
}
bool isIntType(const std::string &t) {
  return t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean || t == kOfxParamTypeChoice ||
         t == kOfxParamTypeInteger2D || t == kOfxParamTypeInteger3D;
}
static bool isStringType(const std::string &t) { return t == kOfxParamTypeString || t == kOfxParamTypeCustom; }

Param *findParam(Effect *e, const char *name) {
  for (auto &p : e->params)
    if (p->name == name) return p.get();
  return nullptr;
}
static Clip *findClip(Effect *e, const char *name) {
  for (auto &c : e->clips)
    if (c->name == name) return c.get();
  return nullptr;
}

static std::unique_ptr<Effect> cloneEffect(const Effect &src) {
  auto e = std::make_unique<Effect>();
  e->props = src.props;
  e->paramSetProps = src.paramSetProps;
  for (auto &p : src.params) e->params.push_back(std::make_unique<Param>(*p));
  for (auto &c : src.clips) {
    auto n = std::make_unique<Clip>(*c);
    n->owner = e.get();
    e->clips.push_back(std::move(n));
  }
  return e;
}

// Parameter suite

static OfxStatus paramDefine(OfxParamSetHandle ps, const char *type, const char *name, OfxPropertySetHandle *props) {
  Effect *e = E(ps);
  if (findParam(e, name)) return kOfxStatErrExists;
  auto p = std::make_unique<Param>();
  p->type = type;
  p->name = name;
  OfxPropertySetHandle h = H(&p->props);
  propSetString(h, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(h, kOfxParamPropType, 0, type);
  propSetString(h, kOfxPropName, 0, name);
  propSetString(h, kOfxPropLabel, 0, name);
  propSetString(h, kOfxParamPropParent, 0, "");
  propSetInt(h, kOfxParamPropEnabled, 0, 1);
  propSetInt(h, kOfxParamPropSecret, 0, 0);
  for (int i = 0; i < dims(type); ++i) propSetDouble(h, kOfxParamPropDefault, i, 0);
  if (isStringType(type)) propSetString(h, kOfxParamPropDefault, 0, "");
  if (props) *props = h;
  e->params.push_back(std::move(p));
  return kOfxStatOK;
}
static OfxStatus paramGetHandle(OfxParamSetHandle ps, const char *name, OfxParamHandle *param, OfxPropertySetHandle *props) {
  Param *p = findParam(E(ps), name);
  if (!p) return kOfxStatErrUnknown;
  *param = reinterpret_cast<OfxParamHandle>(p);
  if (props) *props = H(&p->props);
  return kOfxStatOK;
}
static OfxStatus paramSetGetPropertySet(OfxParamSetHandle ps, OfxPropertySetHandle *props) {
  *props = H(&E(ps)->paramSetProps);
  return kOfxStatOK;
}
static OfxStatus paramGetPropertySet(OfxParamHandle p, OfxPropertySetHandle *props) {
  *props = H(&PA(p)->props);
  return kOfxStatOK;
}
static OfxStatus getValue(Param *p, va_list ap) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (isStringType(p->type)) {
    *va_arg(ap, const char **) = p->s.c_str();
    return kOfxStatOK;
  }
  const bool ints = isIntType(p->type);
  for (double v : p->v) {
    if (ints) *va_arg(ap, int *) = (int)v;
    else *va_arg(ap, double *) = v;
  }
  return kOfxStatOK;
}
static OfxStatus setValue(Param *p, va_list ap) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (isStringType(p->type)) {
    const char *s = va_arg(ap, const char *);
    p->s = s ? s : "";
    return kOfxStatOK;
  }
  const bool ints = isIntType(p->type);
  for (double &v : p->v) v = ints ? va_arg(ap, int) : va_arg(ap, double);
  return kOfxStatOK;
}
static OfxStatus paramGetValue(OfxParamHandle h, ...) {
  va_list ap;
  va_start(ap, h);
  OfxStatus st = getValue(PA(h), ap);
  va_end(ap);
  return st;
}
static OfxStatus paramGetValueAtTime(OfxParamHandle h, OfxTime t, ...) {
  va_list ap;
  va_start(ap, t);
  OfxStatus st = getValue(PA(h), ap);
  va_end(ap);
  return st;
}
static OfxStatus paramSetValue(OfxParamHandle h, ...) {
  va_list ap;
  va_start(ap, h);
  OfxStatus st = setValue(PA(h), ap);
  va_end(ap);
  return st;
}
static OfxStatus paramSetValueAtTime(OfxParamHandle h, OfxTime t, ...) {
  va_list ap;
  va_start(ap, t);
  OfxStatus st = setValue(PA(h), ap);
  va_end(ap);
  return st;
}
static OfxStatus paramNoDerivative(OfxParamHandle, OfxTime, ...) { return kOfxStatErrUnsupported; }
static OfxStatus paramNoIntegral(OfxParamHandle, OfxTime, OfxTime, ...) { return kOfxStatErrUnsupported; }
static OfxStatus paramGetNumKeys(OfxParamHandle, unsigned int *n) {
  *n = 0;
  return kOfxStatOK;
}
static OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime *) { return kOfxStatErrBadIndex; }
static OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int *) { return kOfxStatFailed; }
static OfxStatus paramDeleteKey(OfxParamHandle, OfxTime) { return kOfxStatOK; }
static OfxStatus paramDeleteAllKeys(OfxParamHandle) { return kOfxStatOK; }
static OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime, const OfxRangeD *) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  PA(to)->v = PA(from)->v;
  PA(to)->s = PA(from)->s;
  return kOfxStatOK;
}
static OfxStatus paramEditBegin(OfxParamSetHandle, const char *) { return kOfxStatOK; }
static OfxStatus paramEditEnd(OfxParamSetHandle) { return kOfxStatOK; }

// Image effect suite

static OfxStatus getPropertySet(OfxImageEffectHandle e, OfxPropertySetHandle *props) {
  *props = H(&E(e)->props);
  return kOfxStatOK;
}
static OfxStatus getParamSet(OfxImageEffectHandle e, OfxParamSetHandle *ps) {
  *ps = reinterpret_cast<OfxParamSetHandle>(e);
  return kOfxStatOK;
}
static OfxStatus clipDefine(OfxImageEffectHandle eh, const char *name, OfxPropertySetHandle *props) {
  Effect *e = E(eh);
  auto c = std::make_unique<Clip>();
  c->name = name;
  c->owner = e;
  propSetString(H(&c->props), kOfxPropType, 0, kOfxTypeClip);
  propSetString(H(&c->props), kOfxPropName, 0, name);
  if (props) *props = H(&c->props);
  e->clips.push_back(std::move(c));
  return kOfxStatOK;
}
static OfxStatus clipGetHandle(OfxImageEffectHandle e, const char *name, OfxImageClipHandle *clip, OfxPropertySetHandle *props) {
  Clip *c = findClip(E(e), name);
  if (!c) return kOfxStatErrBadIndex;
  *clip = reinterpret_cast<OfxImageClipHandle>(c);
  if (props) *props = H(&c->props);
  return kOfxStatOK;
}
static OfxStatus clipGetPropertySet(OfxImageClipHandle c, OfxPropertySetHandle *props) {
  *props = H(&C(c)->props);
  return kOfxStatOK;
}
static OfxStatus clipGetImage(OfxImageClipHandle ch, OfxTime, const OfxRectD *, OfxPropertySetHandle *out) {
  Clip *c = C(ch);
  Effect *e = c->owner;
  float *data = c->name == kOfxImageEffectOutputClipName ? e->dst : e->src;
  if (!data) return kOfxStatFailed;
  auto *img = new PropSet;
  OfxPropertySetHandle h = H(img);
  propSetString(h, kOfxPropType, 0, kOfxTypeImage);
  propSetString(h, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
  propSetString(h, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
  propSetString(h, kOfxImageEffectPropPreMultiplication, 0, kOfxImageOpaque);
  propSetString(h, kOfxImagePropField, 0, kOfxImageFieldNone);
  propSetString(h, kOfxImagePropUniqueIdentifier, 0, c->name.c_str());
  const double scale[2] = {1, 1};
  propSetN<double, propSetDouble>(h, kOfxImageEffectPropRenderScale, 2, scale);
  propSetDouble(h, kOfxImagePropPixelAspectRatio, 0, 1);
  propSetPointer(h, kOfxImagePropData, 0, data);
  const int bounds[4] = {0, 0, e->w, e->h};
  propSetN<int, propSetInt>(h, kOfxImagePropBounds, 4, bounds);
  propSetN<int, propSetInt>(h, kOfxImagePropRegionOfDefinition, 4, bounds);
  propSetInt(h, kOfxImagePropRowBytes, 0, e->w * 4 * (int)sizeof(float));
  *out = h;
  return kOfxStatOK;
}
static OfxStatus clipReleaseImage(OfxPropertySetHandle h) {
  delete P(h);
  return kOfxStatOK;
}
static OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle c, OfxTime, OfxRectD *rod) {
  *rod = {0, 0, (double)C(c)->owner->w, (double)C(c)->owner->h};
  return kOfxStatOK;
}
static int effectAbort(OfxImageEffectHandle e) { return E(e)->renderGen && E(e)->renderGen != gLatestGen; }
static OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t n, OfxImageMemoryHandle *h) {
  *h = reinterpret_cast<OfxImageMemoryHandle>(malloc(n));
  return *h ? kOfxStatOK : kOfxStatErrMemory;
}
static OfxStatus imageMemoryFree(OfxImageMemoryHandle h) {
  free(h);
  return kOfxStatOK;
}
static OfxStatus imageMemoryLock(OfxImageMemoryHandle h, void **ptr) {
  *ptr = h;
  return kOfxStatOK;
}
static OfxStatus imageMemoryUnlock(OfxImageMemoryHandle) { return kOfxStatOK; }

// Message, memory, multithread suites

std::function<void(const std::string &)> gOnMessage;

static OfxStatus message(void *, const char *type, const char *, const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);
  va_end(ap);
  type = type ? type : "";
  fprintf(stderr, "OFX %s: %s\n", type, buf);
  if (gOnMessage && (!strcmp(type, kOfxMessageError) || !strcmp(type, kOfxMessageFatal) || !strcmp(type, kOfxMessageWarning)))
    gOnMessage(buf[0] ? buf : "(plugin message)");
  return !strcmp(type, kOfxMessageQuestion) ? kOfxStatReplyYes : kOfxStatOK;
}
static OfxStatus memoryAlloc(void *, size_t n, void **out) {
  *out = malloc(n);
  return *out ? kOfxStatOK : kOfxStatErrMemory;
}
static OfxStatus memoryFree(void *p) {
  free(p);
  return kOfxStatOK;
}

static thread_local unsigned tIndex = 0;
static thread_local bool tSpawned = false;
static unsigned cpuCount() { return std::max(1u, std::thread::hardware_concurrency()); }

static OfxStatus multiThread(OfxThreadFunctionV1 f, unsigned n, void *arg) {
  if (!f) return kOfxStatFailed;
  if (n <= 1) {
    f(0, 1, arg);
    return kOfxStatOK;
  }
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
static OfxStatus mutexTryLock(const OfxMutexHandle m) { return MX(m)->try_lock() ? kOfxStatOK : kOfxStatFailed; }

// Suite tables and host

static const OfxPropertySuiteV1 gPropSuite = [] {
  OfxPropertySuiteV1 s{};
  s.propSetPointer = propSetPointer;
  s.propSetString = propSetString;
  s.propSetDouble = propSetDouble;
  s.propSetInt = propSetInt;
  s.propSetPointerN = propSetN<void *, propSetPointer>;
  s.propSetStringN = propSetN<const char *, propSetString>;
  s.propSetDoubleN = propSetN<double, propSetDouble>;
  s.propSetIntN = propSetN<int, propSetInt>;
  s.propGetPointer = propGetPointer;
  s.propGetString = propGetString;
  s.propGetDouble = propGetDouble;
  s.propGetInt = propGetInt;
  s.propGetPointerN = propGetN<void *, propGetPointer>;
  s.propGetStringN = propGetN<char *, propGetString>;
  s.propGetDoubleN = propGetN<double, propGetDouble>;
  s.propGetIntN = propGetN<int, propGetInt>;
  s.propReset = propReset;
  s.propGetDimension = propGetDimension;
  return s;
}();
static const OfxParameterSuiteV1 gParamSuite = [] {
  OfxParameterSuiteV1 s{};
  s.paramDefine = paramDefine;
  s.paramGetHandle = paramGetHandle;
  s.paramSetGetPropertySet = paramSetGetPropertySet;
  s.paramGetPropertySet = paramGetPropertySet;
  s.paramGetValue = paramGetValue;
  s.paramGetValueAtTime = paramGetValueAtTime;
  s.paramGetDerivative = paramNoDerivative;
  s.paramGetIntegral = paramNoIntegral;
  s.paramSetValue = paramSetValue;
  s.paramSetValueAtTime = paramSetValueAtTime;
  s.paramGetNumKeys = paramGetNumKeys;
  s.paramGetKeyTime = paramGetKeyTime;
  s.paramGetKeyIndex = paramGetKeyIndex;
  s.paramDeleteKey = paramDeleteKey;
  s.paramDeleteAllKeys = paramDeleteAllKeys;
  s.paramCopy = paramCopy;
  s.paramEditBegin = paramEditBegin;
  s.paramEditEnd = paramEditEnd;
  return s;
}();
static const OfxImageEffectSuiteV1 gEffectSuite = [] {
  OfxImageEffectSuiteV1 s{};
  s.getPropertySet = getPropertySet;
  s.getParamSet = getParamSet;
  s.clipDefine = clipDefine;
  s.clipGetHandle = clipGetHandle;
  s.clipGetPropertySet = clipGetPropertySet;
  s.clipGetImage = clipGetImage;
  s.clipReleaseImage = clipReleaseImage;
  s.clipGetRegionOfDefinition = clipGetRegionOfDefinition;
  s.abort = effectAbort;
  s.imageMemoryAlloc = imageMemoryAlloc;
  s.imageMemoryFree = imageMemoryFree;
  s.imageMemoryLock = imageMemoryLock;
  s.imageMemoryUnlock = imageMemoryUnlock;
  return s;
}();
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

static const void *fetchSuite(OfxPropertySetHandle, const char *name, int version) {
  if (version != 1) return nullptr;
  if (!strcmp(name, kOfxPropertySuite)) return &gPropSuite;
  if (!strcmp(name, kOfxParameterSuite)) return &gParamSuite;
  if (!strcmp(name, kOfxImageEffectSuite)) return &gEffectSuite;
  if (!strcmp(name, kOfxMessageSuite)) return &gMessageSuite;
  if (!strcmp(name, kOfxMemorySuite)) return &gMemorySuite;
  if (!strcmp(name, kOfxMultiThreadSuite)) return &gThreadSuite;
  return nullptr;
}

static PropSet gHostProps = [] {
  PropSet ps;
  OfxPropertySetHandle h = H(&ps);
  propSetString(h, kOfxPropName, 0, "local.ofxrawhost");
  propSetString(h, kOfxPropLabel, 0, "OFX Raw Host");
  propSetInt(h, kOfxImageEffectHostPropIsBackground, 0, 0);
  propSetInt(h, kOfxImageEffectPropSupportsOverlays, 0, 0);
  propSetInt(h, kOfxImageEffectPropSupportsMultiResolution, 0, 0);
  propSetInt(h, kOfxImageEffectPropSupportsTiles, 0, 0);
  propSetInt(h, kOfxImageEffectPropTemporalClipAccess, 0, 0);
  propSetInt(h, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  propSetInt(h, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0);
  propSetInt(h, kOfxImageEffectPropSetableFrameRate, 0, 0);
  propSetInt(h, kOfxImageEffectPropSetableFielding, 0, 0);
  propSetString(h, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
  propSetString(h, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
  propSetString(h, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
  propSetInt(h, kOfxParamHostPropSupportsCustomInteract, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsStringAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsChoiceAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsBooleanAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsCustomAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropMaxParameters, 0, -1);
  propSetInt(h, kOfxParamHostPropMaxPages, 0, 0);
  return ps;
}();
static OfxHost gHost = {H(&gHostProps), fetchSuite};

// ------------------------------------------------------------- plugin loading

std::vector<PluginEntry> gPlugins;

OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in) {
  return p->mainEntry(action, e, in ? H(in) : nullptr, nullptr);
}
static bool succeeded(OfxStatus s) { return s == kOfxStatOK || s == kOfxStatReplyDefault; }

static fs::path pluginBinary(const fs::path &bundle) {
  const fs::path contents = bundle / "Contents";
  const std::string stem = bundle.stem().string();  // Foo.ofx
#if defined(_WIN32)
  const char *arch = sizeof(void *) == 8 ? "Win64" : "Win32";
#elif defined(__APPLE__)
  const char *arch = "MacOS";
#elif defined(__aarch64__) || defined(__arm64__)
  const char *arch = "Linux-arm-64";
#elif defined(__x86_64__)
  const char *arch = "Linux-x86-64";
#else
  const char *arch = "Linux-x86";
#endif
  fs::path bin = contents / arch / stem;
  if (fs::exists(bin)) return bin;
#ifdef __APPLE__
  // Universal / arm64 / x86_64 subdirs used by some vendors.
  for (const char *sub : {"MacOS/arm64", "MacOS/x86_64", "MacOS/universal"}) {
    bin = contents / sub / stem;
    if (fs::exists(bin)) return bin;
  }
#endif
  return contents / arch / stem;
}

static void loadBundle(const fs::path &bundle) {
  const fs::path bin = pluginBinary(bundle);
  void *lib = loadLib(bin);
  if (!lib) {
    fprintf(stderr, "Skipping %s: %s\n", bundle.string().c_str(), loadErr());
    return;
  }
  auto setHost = reinterpret_cast<OfxStatus (*)(const OfxHost *)>(sym(lib, "OfxSetHost"));
  auto count = reinterpret_cast<int (*)()>(sym(lib, "OfxGetNumberOfPlugins"));
  auto get = reinterpret_cast<OfxPlugin *(*)(int)>(sym(lib, "OfxGetPlugin"));
  if (!count || !get) return;
  if (setHost) setHost(&gHost);
  for (int i = 0, n = count(); i < n; ++i) {
    OfxPlugin *p = get(i);
    if (!p || strcmp(p->pluginApi, kOfxImageEffectPluginApi) != 0) continue;
    p->setHost(&gHost);
    if (!succeeded(callAction(p, kOfxActionLoad, nullptr))) continue;
    auto base = std::make_unique<Effect>();
    propSetString(H(&base->props), kOfxPropType, 0, kOfxTypeImageEffect);
    if (!succeeded(callAction(p, kOfxActionDescribe, base.get()))) continue;
    bool filter = false;
    for (auto &v : base->props.m[kOfxImageEffectPropSupportedContexts]) filter |= v.s == kOfxImageEffectContextFilter;
    if (!filter) continue;
    auto ctx = cloneEffect(*base);
    PropSet in;
    propSetString(H(&in), kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
    if (!succeeded(callAction(p, kOfxImageEffectActionDescribeInContext, ctx.get(), &in))) continue;
    const std::string label = sprop(base->props, kOfxPropLabel);
    gPlugins.push_back({p, label.empty() ? p->pluginIdentifier : label, std::move(ctx)});
  }
}

void loadPlugins() {
  std::vector<std::string> dirs;
  if (const char *env = getenv("OFX_PLUGIN_PATH")) {
    std::string s = env;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    for (size_t start = 0, end; start <= s.size(); start = end + 1) {
      end = std::min(s.find(sep, start), s.size());
      if (end > start) dirs.push_back(s.substr(start, end - start));
    }
  }
#if defined(_WIN32)
  dirs.push_back("C:\\Program Files\\Common Files\\OFX\\Plugins");
#elif defined(__APPLE__)
  dirs.push_back("/Library/OFX/Plugins");
#else
  dirs.push_back("/usr/OFX/Plugins");
  dirs.push_back("/usr/local/OFX/Plugins");
#endif
  for (auto &d : dirs) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const fs::path &path = it->path();
      if (path.extension() == ".bundle" && path.stem().extension() == ".ofx") {
        loadBundle(path);
        it.disable_recursion_pending();
      }
    }
  }
}

std::unique_ptr<Effect> createInstance(PluginEntry &pe) {
  auto e = cloneEffect(*pe.descriptor);
  OfxPropertySetHandle ep = H(&e->props);
  propSetString(ep, kOfxPropType, 0, kOfxTypeImageEffectInstance);
  propSetString(ep, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
  propSetInt(ep, kOfxPropIsInteractive, 0, 1);
  propSetPointer(ep, kOfxPropInstanceData, 0, nullptr);
  for (auto &p : e->params) {
    p->v.resize(dims(p->type));
    for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = dprop(p->props, kOfxParamPropDefault, (int)i, 0);
    if (isStringType(p->type)) p->s = sprop(p->props, kOfxParamPropDefault);
  }
  const double range[2] = {0, 0};
  for (auto &c : e->clips) {
    OfxPropertySetHandle cp = H(&c->props);
    propSetString(cp, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
    propSetString(cp, kOfxImageClipPropUnmappedPixelDepth, 0, kOfxBitDepthFloat);
    propSetString(cp, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    propSetString(cp, kOfxImageClipPropUnmappedComponents, 0, kOfxImageComponentRGBA);
    propSetString(cp, kOfxImageEffectPropPreMultiplication, 0, kOfxImageOpaque);
    propSetString(cp, kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
    propSetDouble(cp, kOfxImagePropPixelAspectRatio, 0, 1);
    propSetDouble(cp, kOfxImageEffectPropFrameRate, 0, 24);
    propSetN<double, propSetDouble>(cp, kOfxImageEffectPropFrameRange, 2, range);
    propSetN<double, propSetDouble>(cp, kOfxImageEffectPropUnmappedFrameRange, 2, range);
    propSetInt(cp, kOfxImageClipPropConnected, 0, 1);
    propSetInt(cp, kOfxImageClipPropContinuousSamples, 0, 0);
  }
  if (!succeeded(callAction(pe.plugin, kOfxActionCreateInstance, e.get()))) return nullptr;
  return e;
}

OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int gen) {
  e->src = src;
  e->dst = dst;
  e->w = w;
  e->h = h;
  e->renderGen = gen;
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const int window[4] = {0, 0, w, h};
  const double scale[2] = {1, 1};
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetString(a, kOfxImageEffectPropFieldToRender, 0, kOfxImageFieldNone);
  propSetN<int, propSetInt>(a, kOfxImageEffectPropRenderWindow, 4, window);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  propSetInt(a, kOfxImageEffectPropSequentialRenderStatus, 0, 0);
  propSetInt(a, kOfxImageEffectPropInteractiveRenderStatus, 0, gen != 0);
  propSetInt(a, kOfxImageEffectPropRenderQualityDraft, 0, 0);
  const OfxStatus st = callAction(plugin, kOfxImageEffectActionRender, e, &in);
  e->src = e->dst = nullptr;
  return st;
}

