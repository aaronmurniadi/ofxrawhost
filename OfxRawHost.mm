// Minimal still-image OpenFX host: decode RAW/any ImageIO image with Core Image,
// run one OFX filter plugin (CPU buffers, float RGBA), preview, export.

#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ------------------------------------------------------------------ properties

struct Val {
  std::string s;
  double d = 0;
  int i = 0;
  void *p = nullptr;
};
struct PropSet {
  std::map<std::string, std::vector<Val>> m;
};

static PropSet *P(OfxPropertySetHandle h) { return reinterpret_cast<PropSet *>(h); }
static OfxPropertySetHandle H(PropSet *p) { return reinterpret_cast<OfxPropertySetHandle>(p); }

#define CHECK_SET(h, i)                  \
  if (!(h)) return kOfxStatErrBadHandle; \
  if ((i) < 0) return kOfxStatErrBadIndex;

static Val *slot(OfxPropertySetHandle h, const char *k, int i) {
  auto &v = P(h)->m[k];
  if ((int)v.size() <= i) v.resize(i + 1);
  return &v[i];
}
static OfxStatus propSetPointer(OfxPropertySetHandle h, const char *k, int i, void *v) {
  CHECK_SET(h, i);
  slot(h, k, i)->p = v;
  return kOfxStatOK;
}
static OfxStatus propSetString(OfxPropertySetHandle h, const char *k, int i, const char *v) {
  CHECK_SET(h, i);
  slot(h, k, i)->s = v ? v : "";
  return kOfxStatOK;
}
static OfxStatus propSetDouble(OfxPropertySetHandle h, const char *k, int i, double v) {
  CHECK_SET(h, i);
  Val *s = slot(h, k, i);
  s->d = v;
  s->i = std::isfinite(v) ? (int)std::lround(std::clamp(v, (double)INT_MIN, (double)INT_MAX)) : 0;
  return kOfxStatOK;
}
static OfxStatus propSetInt(OfxPropertySetHandle h, const char *k, int i, int v) {
  CHECK_SET(h, i);
  Val *s = slot(h, k, i);
  s->i = v;
  s->d = v;
  return kOfxStatOK;
}
template <class T, OfxStatus (*F)(OfxPropertySetHandle, const char *, int, T)>
static OfxStatus propSetN(OfxPropertySetHandle h, const char *k, int n, const T *v) {
  if (!h) return kOfxStatErrBadHandle;
  P(h)->m[k].resize(std::max(n, 0));
  for (int i = 0; i < n; ++i) F(h, k, i, v[i]);
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

static std::string sprop(const PropSet &ps, const char *k, int i = 0) {
  auto it = ps.m.find(k);
  return it != ps.m.end() && i < (int)it->second.size() ? it->second[i].s : "";
}
static double dprop(const PropSet &ps, const char *k, int i, double fallback) {
  auto it = ps.m.find(k);
  return it != ps.m.end() && i < (int)it->second.size() ? it->second[i].d : fallback;
}

// ------------------------------------------------------ effects, params, clips

struct Param {
  std::string type, name;
  PropSet props;
  std::vector<double> v;
  std::string s;
};
struct Effect;
struct Clip {
  std::string name;
  PropSet props;
  Effect *owner = nullptr;
};
struct Effect {
  PropSet props, paramSetProps;
  std::vector<std::unique_ptr<Param>> params;
  std::vector<std::unique_ptr<Clip>> clips;
  float *src = nullptr, *dst = nullptr;
  int w = 0, h = 0, renderGen = 0;
};

static std::atomic<int> gLatestGen{0};
static std::mutex gValueMutex;

static Effect *E(OfxImageEffectHandle h) { return reinterpret_cast<Effect *>(h); }
static Effect *E(OfxParamSetHandle h) { return reinterpret_cast<Effect *>(h); }
static Param *PA(OfxParamHandle h) { return reinterpret_cast<Param *>(h); }
static Clip *C(OfxImageClipHandle h) { return reinterpret_cast<Clip *>(h); }

static int dims(const std::string &t) {
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean || t == kOfxParamTypeChoice) return 1;
  if (t == kOfxParamTypeDouble2D || t == kOfxParamTypeInteger2D) return 2;
  if (t == kOfxParamTypeDouble3D || t == kOfxParamTypeInteger3D || t == kOfxParamTypeRGB) return 3;
  if (t == kOfxParamTypeRGBA) return 4;
  return 0;
}
static bool isIntType(const std::string &t) {
  return t == kOfxParamTypeInteger || t == kOfxParamTypeBoolean || t == kOfxParamTypeChoice ||
         t == kOfxParamTypeInteger2D || t == kOfxParamTypeInteger3D;
}
static bool isStringType(const std::string &t) { return t == kOfxParamTypeString || t == kOfxParamTypeCustom; }

static Param *findParam(Effect *e, const char *name) {
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

static void (^gOnMessage)(NSString *) = nil;

static OfxStatus message(void *, const char *type, const char *, const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt ? fmt : "", ap);
  va_end(ap);
  type = type ? type : "";
  NSLog(@"OFX %s: %s", type, buf);
  if (gOnMessage && (!strcmp(type, kOfxMessageError) || !strcmp(type, kOfxMessageFatal) || !strcmp(type, kOfxMessageWarning)))
    gOnMessage([NSString stringWithUTF8String:buf] ?: @"(plugin message)");
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

struct PluginEntry {
  OfxPlugin *plugin;
  std::string label;
  std::unique_ptr<Effect> descriptor;  // filter-context descriptor
};
static std::vector<PluginEntry> gPlugins;

static OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in = nullptr) {
  return p->mainEntry(action, e, in ? H(in) : nullptr, nullptr);
}
static bool succeeded(OfxStatus s) { return s == kOfxStatOK || s == kOfxStatReplyDefault; }

static void loadBundle(const fs::path &bundle) {
  const fs::path bin = bundle / "Contents" / "MacOS" / bundle.stem();
  void *lib = dlopen(bin.c_str(), RTLD_LAZY | RTLD_LOCAL);
  if (!lib) {
    NSLog(@"Skipping %s: %s", bundle.c_str(), dlerror());
    return;
  }
  auto setHost = reinterpret_cast<OfxStatus (*)(const OfxHost *)>(dlsym(lib, "OfxSetHost"));
  auto count = reinterpret_cast<int (*)()>(dlsym(lib, "OfxGetNumberOfPlugins"));
  auto get = reinterpret_cast<OfxPlugin *(*)(int)>(dlsym(lib, "OfxGetPlugin"));
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

static void loadPlugins() {
  std::vector<std::string> dirs;
  if (const char *env = getenv("OFX_PLUGIN_PATH")) {
    std::string s = env;
    for (size_t start = 0, end; start <= s.size(); start = end + 1) {
      end = std::min(s.find(':', start), s.size());
      if (end > start) dirs.push_back(s.substr(start, end - start));
    }
  }
  dirs.push_back("/Library/OFX/Plugins");
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

static std::unique_ptr<Effect> createInstance(PluginEntry &pe) {
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

// src/dst: bottom-up float RGBA, w*h pixels. gen 0 = never aborted.
static OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int gen) {
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

// --------------------------------------------------------------- image I/O

using Pixels = std::shared_ptr<std::vector<float>>;

static CIImage *loadImage(NSURL *url) {
  if (CIRAWFilter *raw = [CIRAWFilter filterWithImageURL:url]) {
    raw.boostAmount = 0;  // linear, no film-like tone curve
    if (raw.localToneMapSupported) raw.localToneMapAmount = 0;
    return raw.outputImage;
  }
  return [CIImage imageWithContentsOfURL:url options:@{kCIImageApplyOrientationProperty : @YES}];
}

static void flipRows(float *px, int w, int h) {
  const size_t row = (size_t)w * 4;
  std::vector<float> tmp(row);
  for (int y = 0; y < h / 2; ++y) {
    float *a = px + y * row, *b = px + (size_t)(h - 1 - y) * row;
    std::copy(a, a + row, tmp.data());
    std::copy(b, b + row, a);
    std::copy(tmp.begin(), tmp.end(), b);
  }
}

// Renders to scene-linear Rec.2020, bottom-up rows (OFX order). maxEdge 0 = full size.
static Pixels renderSource(CIContext *ctx, CIImage *img, int maxEdge, int &w, int &h) {
  CGRect ext = img.extent;
  const double longEdge = std::max(ext.size.width, ext.size.height);
  if (maxEdge > 0 && longEdge > maxEdge) {
    img = [img imageByApplyingFilter:@"CILanczosScaleTransform"
                 withInputParameters:@{kCIInputScaleKey : @(maxEdge / longEdge), kCIInputAspectRatioKey : @1}];
    ext = img.extent;
  }
  img = [img imageByApplyingTransform:CGAffineTransformMakeTranslation(-ext.origin.x, -ext.origin.y)];
  w = (int)std::floor(ext.size.width);
  h = (int)std::floor(ext.size.height);
  auto px = std::make_shared<std::vector<float>>((size_t)w * h * 4);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020);
  [ctx render:img toBitmap:px->data() rowBytes:w * 16 bounds:CGRectMake(0, 0, w, h) format:kCIFormatRGBAf colorSpace:cs];
  CGColorSpaceRelease(cs);
  flipRows(px->data(), w, h);  // Core Image writes top row first
  return px;
}

// Wraps bottom-up float RGBA as a top-down CGImage tagged with `space`.
static CGImageRef makeCGImage(const std::vector<float> &px, int w, int h, CFStringRef space) {
  NSMutableData *data = [NSMutableData dataWithBytes:px.data() length:px.size() * sizeof(float)];
  flipRows(static_cast<float *>(data.mutableBytes), w, h);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(space);
  CGImageRef img = CGImageCreate(w, h, 32, 128, w * 16, cs,
                                 kCGImageAlphaNoneSkipLast | kCGBitmapFloatComponents | kCGBitmapByteOrder32Host,
                                 provider, nullptr, false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(cs);
  CGDataProviderRelease(provider);
  return img;
}

static CGImageRef to16Bit(CGImageRef img) {
  CGContextRef ctx = CGBitmapContextCreate(nullptr, CGImageGetWidth(img), CGImageGetHeight(img), 16, 0,
                                           CGImageGetColorSpace(img), kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder16Host);
  if (!ctx) return CGImageRetain(img);
  CGContextDrawImage(ctx, CGRectMake(0, 0, CGImageGetWidth(img), CGImageGetHeight(img)), img);
  CGImageRef out = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  return out;
}

static bool writeImage(CGImageRef img, NSURL *url) {
  UTType *type = [UTType typeWithFilenameExtension:url.pathExtension] ?: UTTypeTIFF;
  const bool keepFloat = [type conformsToType:[UTType typeWithIdentifier:@"com.ilm.openexr-image"]];
  CGImageRef out = keepFloat ? CGImageRetain(img) : to16Bit(img);
  CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)type.identifier, 1, nullptr);
  bool ok = false;
  if (dest) {
    CGImageDestinationAddImage(dest, out, (__bridge CFDictionaryRef) @{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality : @0.92});
    ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
  }
  CGImageRelease(out);
  return ok;
}

// ------------------------------------------------------------------------ UI

static const struct {
  NSString *label;
  CFStringRef space;
} kOutputSpaces[] = {
  {@"sRGB", kCGColorSpaceSRGB},
  {@"Display P3", kCGColorSpaceDisplayP3},
  {@"Linear Rec.709", kCGColorSpaceLinearSRGB},
  {@"Linear Rec.2020", kCGColorSpaceLinearITUR_2020},
};

static NSString *NS(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

struct Row {
  Param *p;
  NSArray<NSControl *> *controls;
  NSView *view;
};

@interface FlippedView : NSView
@end
@implementation FlippedView
- (BOOL)isFlipped {
  return YES;
}
@end

@interface Controller : NSObject <NSApplicationDelegate>
@end

@implementation Controller {
  NSWindow *_window;
  NSImageView *_imageView;
  NSStackView *_paramStack;
  NSPopUpButton *_pluginPopup, *_outputPopup;
  NSTextField *_status;
  CIContext *_ciContext;
  NSURL *_imageURL;
  CIImage *_image;
  Pixels _preview;
  int _pw, _ph;
  int _pluginIndex;
  std::unique_ptr<Effect> _instance;
  std::vector<Row> _rows;
  std::map<std::string, bool> _groupOpen;
  dispatch_queue_t _renderQueue;
  NSSavePanel *_savePanel;
  NSInteger _exportFormat;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
  _ciContext = [CIContext contextWithOptions:@{kCIContextWorkingFormat : @(kCIFormatRGBAf)}];
  _renderQueue = dispatch_queue_create("render", DISPATCH_QUEUE_SERIAL);
  _pluginIndex = -1;
  [self buildMenu];
  [self buildWindow];
  __weak Controller *weakSelf = self;
  gOnMessage = ^(NSString *msg) {
    dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf setStatus:msg]; });
  };
  loadPlugins();
  for (auto &pe : gPlugins) [_pluginPopup addItemWithTitle:NS(pe.label)];
  if (gPlugins.empty()) [self setStatus:@"No OFX filter plugins found in /Library/OFX/Plugins or OFX_PLUGIN_PATH"];
  else [self pluginChanged:_pluginPopup];
  NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
  if (args.count > 1 && ![args[1] hasPrefix:@"-"]) [self openURL:[NSURL fileURLWithPath:args[1]]];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
  return YES;
}

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls {
  [self openURL:urls.firstObject];
}

- (void)buildMenu {
  NSMenu *bar = [NSMenu new];
  NSMenuItem *appItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
  appItem.submenu = [NSMenu new];
  [appItem.submenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
  NSMenuItem *fileItem = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
  fileItem.submenu = [[NSMenu alloc] initWithTitle:@"File"];
  [fileItem.submenu addItemWithTitle:@"Open…" action:@selector(openDocument:) keyEquivalent:@"o"];
  [fileItem.submenu addItemWithTitle:@"Export…" action:@selector(exportDocument:) keyEquivalent:@"e"];
  NSApp.mainMenu = bar;
}

- (void)buildWindow {
  _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1400, 900)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.title = @"OFX Raw Host";

  _pluginPopup = [NSPopUpButton new];
  _pluginPopup.target = self;
  _pluginPopup.action = @selector(pluginChanged:);
  _outputPopup = [NSPopUpButton new];
  for (auto &o : kOutputSpaces) [_outputPopup addItemWithTitle:o.label];
  _outputPopup.target = self;
  _outputPopup.action = @selector(scheduleRender);
  _status = [NSTextField labelWithString:@"Open an image with ⌘O. Source is fed to the plugin as scene-linear Rec.2020."];
  _status.lineBreakMode = NSLineBreakByTruncatingTail;
  [_status setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView *top = [NSStackView stackViewWithViews:@[
    [NSButton buttonWithTitle:@"Open…" target:self action:@selector(openDocument:)],
    [NSButton buttonWithTitle:@"Export…" target:self action:@selector(exportDocument:)],
    [NSTextField labelWithString:@"Plugin:"], _pluginPopup, [NSTextField labelWithString:@"Output tag:"], _outputPopup, _status
  ]];

  _imageView = [NSImageView new];
  _imageView.imageScaling = NSImageScaleProportionallyUpOrDown;
  for (NSLayoutConstraintOrientation o : {NSLayoutConstraintOrientationHorizontal, NSLayoutConstraintOrientationVertical}) {
    [_imageView setContentCompressionResistancePriority:1 forOrientation:o];
    [_imageView setContentHuggingPriority:1 forOrientation:o];
  }

  _paramStack = [NSStackView new];
  _paramStack.orientation = NSUserInterfaceLayoutOrientationVertical;
  _paramStack.alignment = NSLayoutAttributeLeading;
  _paramStack.spacing = 4;
  _paramStack.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  _paramStack.translatesAutoresizingMaskIntoConstraints = NO;
  FlippedView *doc = [FlippedView new];
  doc.translatesAutoresizingMaskIntoConstraints = NO;
  [doc addSubview:_paramStack];
  NSScrollView *scroll = [NSScrollView new];
  scroll.hasVerticalScroller = YES;
  scroll.documentView = doc;
  [NSLayoutConstraint activateConstraints:@[
    [_paramStack.topAnchor constraintEqualToAnchor:doc.topAnchor],
    [_paramStack.leadingAnchor constraintEqualToAnchor:doc.leadingAnchor],
    [_paramStack.trailingAnchor constraintEqualToAnchor:doc.trailingAnchor],
    [_paramStack.bottomAnchor constraintEqualToAnchor:doc.bottomAnchor],
    [doc.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
    [scroll.widthAnchor constraintGreaterThanOrEqualToConstant:460],
  ]];

  NSSplitView *split = [NSSplitView new];
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  [split addArrangedSubview:_imageView];
  [split addArrangedSubview:scroll];
  [split setHoldingPriority:NSLayoutPriorityDefaultLow - 1 forSubviewAtIndex:0];

  NSStackView *root = [NSStackView stackViewWithViews:@[ top, split ]];
  root.orientation = NSUserInterfaceLayoutOrientationVertical;
  root.alignment = NSLayoutAttributeLeading;
  root.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  root.translatesAutoresizingMaskIntoConstraints = NO;
  [_window.contentView addSubview:root];
  NSView *content = _window.contentView;
  [NSLayoutConstraint activateConstraints:@[
    [root.topAnchor constraintEqualToAnchor:content.topAnchor],
    [root.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
    [root.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [root.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [split.widthAnchor constraintEqualToAnchor:root.widthAnchor constant:-16],
    [top.widthAnchor constraintEqualToAnchor:split.widthAnchor],
  ]];
  [_window center];
  [_window makeKeyAndOrderFront:nil];
}

- (void)setStatus:(NSString *)text {
  _status.stringValue = text;
}

// -- plugin instance

- (void)destroyInstance {
  if (!_instance) return;
  ++gLatestGen;
  dispatch_sync(_renderQueue, ^{});
  callAction(gPlugins[_pluginIndex].plugin, kOfxActionDestroyInstance, _instance.get());
  _instance.reset();
}

- (void)pluginChanged:(NSPopUpButton *)sender {
  [self destroyInstance];
  _pluginIndex = (int)sender.indexOfSelectedItem;
  if (_pluginIndex < 0) return;
  _instance = createInstance(gPlugins[_pluginIndex]);
  if (!_instance) [self setStatus:@"Plugin failed to create an instance"];
  if (_instance && _preview) {
    _instance->w = _pw;
    _instance->h = _ph;
  }
  _groupOpen.clear();
  if (_instance) {
    [self applyColorDefaults];
    [self syncOutputTag];
  }
  if (_instance)
    for (auto &p : _instance->params)
      if (p->type == kOfxParamTypeGroup) _groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  [self buildParamUI];
  [self scheduleRender];
}

// -- parameter panel

- (void)buildParamUI {
  for (NSView *v in _paramStack.arrangedSubviews.copy) [v removeFromSuperview];
  _rows.clear();
  if (_instance) [self addParamsWithParent:""];
  [self refreshControls];
}

- (void)addParamsWithParent:(const std::string &)parent {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (p->type == kOfxParamTypeGroup) {
      NSButton *header = [NSButton buttonWithTitle:@"" target:self action:@selector(toggleGroup:)];
      header.bordered = NO;
      header.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize];
      header.tag = (NSInteger)_rows.size();
      [_paramStack addArrangedSubview:header];
      _rows.push_back({p, @[ header ], header});
      [self addParamsWithParent:p->name];
    } else {
      [self addRowFor:p];
    }
  }
}

- (NSTextField *)numberField {
  NSTextField *f = [NSTextField textFieldWithString:@""];
  f.target = self;
  f.action = @selector(changed:);
  [f.widthAnchor constraintEqualToConstant:64].active = YES;
  return f;
}

- (void)addRowFor:(Param *)p {
  const std::string &t = p->type;
  NSString *label = NS(sprop(p->props, kOfxPropLabel));
  NSMutableArray<NSControl *> *controls = [NSMutableArray array];
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
    double lo = dprop(p->props, kOfxParamPropDisplayMin, 0, dprop(p->props, kOfxParamPropMin, 0, 0));
    double hi = dprop(p->props, kOfxParamPropDisplayMax, 0, dprop(p->props, kOfxParamPropMax, 0, t == kOfxParamTypeInteger ? 100 : 1));
    if (!(std::fabs(lo) < 1e7)) lo = 0;
    if (!(std::fabs(hi) < 1e7) || hi <= lo) hi = lo + (t == kOfxParamTypeInteger ? 100 : 1);
    NSSlider *slider = [NSSlider sliderWithValue:lo minValue:lo maxValue:hi target:self action:@selector(changed:)];
    [slider.widthAnchor constraintGreaterThanOrEqualToConstant:140].active = YES;
    [controls addObjectsFromArray:@[ slider, [self numberField] ]];
  } else if (t == kOfxParamTypeBoolean) {
    [controls addObject:[NSButton checkboxWithTitle:@"" target:self action:@selector(changed:)]];
  } else if (t == kOfxParamTypeChoice) {
    NSPopUpButton *popup = [NSPopUpButton new];
    auto it = p->props.m.find(kOfxParamPropChoiceOption);
    if (it != p->props.m.end())
      for (auto &o : it->second) [popup addItemWithTitle:NS(o.s)];
    popup.target = self;
    popup.action = @selector(changed:);
    [controls addObject:popup];
  } else if (t == kOfxParamTypePushButton) {
    [controls addObject:[NSButton buttonWithTitle:label target:self action:@selector(changed:)]];
    label = @"";
  } else if (t == kOfxParamTypeString) {
    NSTextField *f = [NSTextField textFieldWithString:@""];
    f.editable = sprop(p->props, kOfxParamPropStringMode) != kOfxParamStringIsLabel;
    f.target = self;
    f.action = @selector(changed:);
    [f.widthAnchor constraintGreaterThanOrEqualToConstant:200].active = YES;
    [controls addObject:f];
  } else if (dims(t) > 1) {
    for (int i = 0; i < dims(t); ++i) [controls addObject:[self numberField]];
  } else {
    return;  // custom / parametric params are not supported
  }
  NSTextField *labelField = [NSTextField labelWithString:label];
  labelField.lineBreakMode = NSLineBreakByTruncatingTail;
  labelField.toolTip = NS(sprop(p->props, kOfxParamPropHint));
  [labelField.widthAnchor constraintEqualToConstant:170].active = YES;
  for (NSControl *c in controls) c.tag = (NSInteger)_rows.size();
  NSStackView *row = [NSStackView stackViewWithViews:[@[ labelField ] arrayByAddingObjectsFromArray:controls]];
  [_paramStack addArrangedSubview:row];
  _rows.push_back({p, controls, row});
}

- (bool)ancestorsOpen:(const std::string &)group {
  if (group.empty()) return true;
  Param *g = findParam(_instance.get(), group.c_str());
  return g && _groupOpen[group] && [self ancestorsOpen:sprop(g->props, kOfxParamPropParent)];
}

// No gValueMutex here: hiding a focused field ends editing, which re-enters -changed: and takes the lock.
// Values are only written on the main thread, so main-thread reads need no lock.
- (void)refreshControls {
  for (Row &r : _rows) {
    Param *p = r.p;
    r.view.hidden = dprop(p->props, kOfxParamPropSecret, 0, 0) != 0 || ![self ancestorsOpen:sprop(p->props, kOfxParamPropParent)];
    const bool enabled = dprop(p->props, kOfxParamPropEnabled, 0, 1) != 0;
    for (NSControl *c in r.controls) c.enabled = enabled;
    const std::string &t = p->type;
    if (t == kOfxParamTypeGroup) {
      NSButton *b = (NSButton *)r.controls[0];
      b.title = [NSString stringWithFormat:@"%@ %@", _groupOpen[p->name] ? @"▾" : @"▸", NS(sprop(p->props, kOfxPropLabel))];
    } else if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
      r.controls[0].doubleValue = p->v[0];
      r.controls[1].stringValue = [NSString stringWithFormat:@"%.4g", p->v[0]];
    } else if (t == kOfxParamTypeBoolean) {
      ((NSButton *)r.controls[0]).state = p->v[0] != 0 ? NSControlStateValueOn : NSControlStateValueOff;
    } else if (t == kOfxParamTypeChoice) {
      [(NSPopUpButton *)r.controls[0] selectItemAtIndex:(NSInteger)p->v[0]];
    } else if (t == kOfxParamTypeString) {
      r.controls[0].stringValue = NS(p->s);
    } else if (t != kOfxParamTypePushButton) {
      for (size_t i = 0; i < p->v.size(); ++i) r.controls[i].stringValue = [NSString stringWithFormat:@"%.4g", p->v[i]];
    }
  }
}

- (void)toggleGroup:(NSButton *)sender {
  const std::string &name = _rows[sender.tag].p->name;
  _groupOpen[name] = !_groupOpen[name];
  [self refreshControls];
}

- (void)changed:(NSControl *)sender {
  Row &r = _rows[sender.tag];
  Param *p = r.p;
  const std::string &t = p->type;
  {
    std::lock_guard<std::mutex> lock(gValueMutex);
    if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
      double v = sender.doubleValue;
      p->v[0] = t == kOfxParamTypeInteger ? std::round(v) : v;
    } else if (t == kOfxParamTypeBoolean) {
      p->v[0] = ((NSButton *)sender).state == NSControlStateValueOn;
    } else if (t == kOfxParamTypeChoice) {
      p->v[0] = (double)((NSPopUpButton *)sender).indexOfSelectedItem;
    } else if (t == kOfxParamTypeString) {
      p->s = sender.stringValue.UTF8String;
    } else if (t != kOfxParamTypePushButton) {
      for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = isIntType(t) ? std::round(r.controls[i].doubleValue) : r.controls[i].doubleValue;
    }
  }
  [self notifyChanged:p];
  [self syncOutputTag];
  [self refreshControls];
  [self scheduleRender];
}

- (void)notifyChanged:(Param *)p {
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const double scale[2] = {1, 1};
  propSetString(a, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(a, kOfxPropName, 0, p->name.c_str());
  propSetString(a, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  callAction(plugin, kOfxActionBeginInstanceChanged, _instance.get(), &in);
  callAction(plugin, kOfxActionInstanceChanged, _instance.get(), &in);
  callAction(plugin, kOfxActionEndInstanceChanged, _instance.get(), &in);
}

static const std::vector<Val> &choiceOptions(Param *p) {
  static const std::vector<Val> none;
  auto it = p->props.m.find(kOfxParamPropChoiceOption);
  return it != p->props.m.end() ? it->second : none;
}

// The host always feeds scene-linear Rec.2020; default the plugin's output to web-safe sRGB.
- (void)applyColorDefaults {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice) continue;
    const std::string label = sprop(p->props, kOfxPropLabel);
    const char *want = label == "Input Color Space" ? "Linear Rec.2020" : label == "Output Color Space" ? "sRGB" : nullptr;
    const auto &options = choiceOptions(p);
    for (size_t i = 0; want && i < options.size(); ++i) {
      if (options[i].s != want) continue;
      {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[0] = (double)i;
      }
      [self notifyChanged:p];
      break;
    }
  }
}

// Tags display/export with the plugin's visible output color space when the host knows it.
- (void)syncOutputTag {
  for (auto &up : _instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice || sprop(p->props, kOfxPropLabel) != "Output Color Space" ||
        dprop(p->props, kOfxParamPropSecret, 0, 0) != 0)
      continue;
    const auto &options = choiceOptions(p);
    const size_t index = (size_t)p->v[0];
    if (index < options.size()) [_outputPopup selectItemWithTitle:NS(options[index].s)];
    if (_outputPopup.indexOfSelectedItem < 0) [_outputPopup selectItemAtIndex:0];
  }
}

// -- rendering

- (CFStringRef)outputSpace {
  return kOutputSpaces[std::max<NSInteger>(0, _outputPopup.indexOfSelectedItem)].space;
}

- (void)scheduleRender {
  if (!_instance || !_preview) return;
  const int gen = ++gLatestGen;
  Effect *effect = _instance.get();
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  Pixels src = _preview;
  const int w = _pw, h = _ph;
  CFStringRef space = [self outputSpace];
  [self setStatus:@"Rendering…"];
  dispatch_async(_renderQueue, ^{
    if (gen != gLatestGen) return;
    std::vector<float> out(src->size());
    const OfxStatus st = renderEffect(plugin, effect, src->data(), out.data(), w, h, gen);
    if (gen != gLatestGen) return;
    CGImageRef cg = st == kOfxStatOK ? makeCGImage(out, w, h, space) : nullptr;
    NSImage *img = cg ? [[NSImage alloc] initWithCGImage:cg size:NSMakeSize(w, h)] : nil;
    CGImageRelease(cg);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (gen != gLatestGen) return;
      if (img) self->_imageView.image = img;
      [self setStatus:img ? [NSString stringWithFormat:@"%d×%d preview", w, h]
                          : [NSString stringWithFormat:@"Render failed (OFX status %d)", st]];
    });
  });
}

// -- documents

- (void)openDocument:(id)sender {
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  panel.allowedContentTypes = @[ UTTypeImage ];
  if ([panel runModal] == NSModalResponseOK) [self openURL:panel.URL];
}

- (void)openURL:(NSURL *)url {
  CIImage *img = url ? loadImage(url) : nil;
  if (!img) {
    [self setStatus:[NSString stringWithFormat:@"Could not decode %@", url.lastPathComponent]];
    return;
  }
  _imageURL = url;
  _image = img;
  _preview = renderSource(_ciContext, img, 1600, _pw, _ph);
  if (_instance) {
    _instance->w = _pw;
    _instance->h = _ph;
  }
  _window.title = url.lastPathComponent;
  [self scheduleRender];
}

- (void)exportFormatChanged:(NSPopUpButton *)sender {
  _exportFormat = std::max<NSInteger>(0, sender.indexOfSelectedItem);
  UTType *types[] = {UTTypeTIFF, UTTypePNG, UTTypeJPEG, [UTType typeWithIdentifier:@"com.ilm.openexr-image"]};
  _savePanel.allowedContentTypes = @[ types[_exportFormat] ];
}

- (void)exportDocument:(id)sender {
  if (!_image || !_instance) return;
  NSSavePanel *panel = [NSSavePanel savePanel];
  _savePanel = panel;
  NSPopUpButton *format = [NSPopUpButton new];
  [format addItemsWithTitles:@[ @"TIFF (16-bit)", @"PNG (16-bit)", @"JPEG", @"OpenEXR (float)" ]];
  [format selectItemAtIndex:_exportFormat];
  format.target = self;
  format.action = @selector(exportFormatChanged:);
  NSStackView *accessory = [NSStackView stackViewWithViews:@[ [NSTextField labelWithString:@"Format:"], format ]];
  accessory.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
  panel.accessoryView = accessory;
  panel.nameFieldStringValue = _imageURL.lastPathComponent.stringByDeletingPathExtension;
  [self exportFormatChanged:format];
  const NSModalResponse response = [panel runModal];
  _savePanel = nil;
  if (response != NSModalResponseOK) return;
  NSURL *url = panel.URL;
  CIImage *image = _image;
  CIContext *ciContext = _ciContext;
  Effect *effect = _instance.get();
  OfxPlugin *plugin = gPlugins[_pluginIndex].plugin;
  CFStringRef space = [self outputSpace];
  const int pw = _pw, ph = _ph;
  [self setStatus:@"Exporting full resolution…"];
  ++gLatestGen;
  dispatch_async(_renderQueue, ^{
    int w = 0, h = 0;
    Pixels src = renderSource(ciContext, image, 0, w, h);
    std::vector<float> out(src->size());
    OfxStatus st = renderEffect(plugin, effect, src->data(), out.data(), w, h, 0);
    effect->w = pw;
    effect->h = ph;
    bool ok = false;
    if (st == kOfxStatOK) {
      CGImageRef cg = makeCGImage(out, w, h, space);
      ok = writeImage(cg, url);
      CGImageRelease(cg);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      [self setStatus:ok ? [NSString stringWithFormat:@"Exported %@ (%d×%d)", url.lastPathComponent, w, h]
                         : [NSString stringWithFormat:@"Export failed (OFX status %d)", st]];
    });
  });
}

@end

// ----------------------------------------------------------------- self-test

static int fail(const char *msg) {
  fprintf(stderr, "selftest FAILED: %s\n", msg);
  return 1;
}

// Checks Core Image row order and renders a gray ramp through every installed filter plugin.
static int selfTest() {
  CIContext *ctx = [CIContext contextWithOptions:@{kCIContextWorkingFormat : @(kCIFormatRGBAf)}];
  CIImage *topRed = [[[CIImage imageWithColor:CIColor.redColor] imageByCroppingToRect:CGRectMake(0, 4, 8, 4)]
      imageByCompositingOverImage:[[CIImage imageWithColor:CIColor.blackColor] imageByCroppingToRect:CGRectMake(0, 0, 8, 8)]];
  int w = 0, h = 0;
  Pixels px = renderSource(ctx, topRed, 0, w, h);
  if (w != 8 || h != 8 || (*px)[0] > 0.1f || (*px)[(size_t)7 * 8 * 4] < 0.5f) return fail("source rows are not bottom-up");

  loadPlugins();
  if (gPlugins.empty()) return fail("no OFX filter plugins found");
  for (auto &pe : gPlugins) {
    auto e = createInstance(pe);
    if (!e) return fail(("createInstance: " + pe.label).c_str());
    w = 64;
    h = 48;
    std::vector<float> src((size_t)w * h * 4, 1.0f), out(src.size(), -1.0f);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 3; ++c) src[((size_t)y * w + x) * 4 + c] = 0.18f * std::exp2((x - w / 2) / 8.0f);
    const OfxStatus st = renderEffect(pe.plugin, e.get(), src.data(), out.data(), w, h, 0);
    callAction(pe.plugin, kOfxActionDestroyInstance, e.get());
    if (st != kOfxStatOK) return fail(("render: " + pe.label).c_str());
    bool finite = true, touched = false;
    for (float v : out) {
      finite &= std::isfinite(v);
      touched |= v != -1.0f;
    }
    if (!finite || !touched) return fail(("output: " + pe.label).c_str());
    CGImageRef cg = makeCGImage(out, w, h, kCGColorSpaceSRGB);
    bool written = true;
    for (NSString *ext in @[ @"tif", @"png", @"jpg", @"exr" ])
      written &= writeImage(cg, [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
                                                                                  [@"ofxrawhost-selftest." stringByAppendingString:ext]]]);
    CGImageRelease(cg);
    if (!written) return fail("export");
    printf("ok  %s\n", pe.label.c_str());
  }
  return 0;
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selfTest();
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    Controller *controller = [Controller new];
    app.delegate = controller;
    [app run];
  }
  return 0;
}
