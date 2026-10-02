#include "ofx/OfxHost.h"
#include "ofx/OfxHostPriv.h"
#include "ofx/OfxMetal.h"

#include "ofxGPURender.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxParametricParam.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

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
  if (!h || !count) return kOfxStatErrBadHandle;
  auto it = P(h)->m.find(k);
  // Missing property => dimension 0 (OFX Support addSupportedBitDepth reads this first).
  *count = it == P(h)->m.end() ? 0 : (int)it->second.size();
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

ParamType paramTypeFromString(const std::string &type) {
  // One table maps the OFX type string to its kind. Add a row here to teach the
  // host a new parameter type.
  static const struct {
    const char *name;
    ParamType type;
  } kTypes[] = {
      {kOfxParamTypeInteger, ParamType::Integer},
      {kOfxParamTypeDouble, ParamType::Double},
      {kOfxParamTypeBoolean, ParamType::Boolean},
      {kOfxParamTypeChoice, ParamType::Choice},
      {kOfxParamTypeString, ParamType::String},
      {kOfxParamTypeCustom, ParamType::Custom},
      {kOfxParamTypeStrChoice, ParamType::StrChoice},
      {kOfxParamTypeBytes, ParamType::Bytes},
      {kOfxParamTypePushButton, ParamType::PushButton},
      {kOfxParamTypeGroup, ParamType::Group},
      {kOfxParamTypePage, ParamType::Page},
      {kOfxParamTypeInteger2D, ParamType::Integer2D},
      {kOfxParamTypeDouble2D, ParamType::Double2D},
      {kOfxParamTypeInteger3D, ParamType::Integer3D},
      {kOfxParamTypeDouble3D, ParamType::Double3D},
      {kOfxParamTypeRGB, ParamType::RGB},
      {kOfxParamTypeRGBA, ParamType::RGBA},
  };
  for (const auto &row : kTypes)
    if (type == row.name) return row.type;
  return ParamType::Unknown;
}
int paramDimension(ParamType t) {
  switch (t) {
    case ParamType::Integer:
    case ParamType::Double:
    case ParamType::Boolean:
    case ParamType::Choice: return 1;
    case ParamType::Integer2D:
    case ParamType::Double2D: return 2;
    case ParamType::Integer3D:
    case ParamType::Double3D:
    case ParamType::RGB: return 3;
    case ParamType::RGBA: return 4;
    default: return 0;
  }
}
bool paramIsInteger(ParamType t) {
  switch (t) {
    case ParamType::Integer:
    case ParamType::Boolean:
    case ParamType::Choice:
    case ParamType::Integer2D:
    case ParamType::Integer3D: return true;
    default: return false;
  }
}
bool paramIsString(ParamType t) { return t == ParamType::String || t == ParamType::Custom; }

// Thin wrappers for the call sites that hold only the OFX type string.
int dims(const std::string &t) { return paramDimension(paramTypeFromString(t)); }
bool isIntType(const std::string &t) { return paramIsInteger(paramTypeFromString(t)); }
bool isStringType(const std::string &t) { return paramIsString(paramTypeFromString(t)); }

Param *findParam(Effect *e, const char *name) {
  for (auto &p : e->params)
    if (p->name == name) return p.get();
  return nullptr;
}

void refreshParamUiCache(Effect *e) {
  for (auto &up : e->params) {
    Param *p = up.get();
    ParamUiCache &ui = p->ui;
    ui.label = sprop(p->props, kOfxPropLabel);
    ui.hint = sprop(p->props, kOfxParamPropHint);
    ui.parent = sprop(p->props, kOfxParamPropParent);
    const std::string base = ui.label.empty() ? std::string("param") : ui.label;
    ui.idLabel = base + "##" + p->name;
    ui.enabled = dprop(p->props, kOfxParamPropEnabled, 0, 1) != 0;
    ui.secret = dprop(p->props, kOfxParamPropSecret, 0, 0) != 0;
    ui.stringIsLabel = sprop(p->props, kOfxParamPropStringMode) == kOfxParamStringIsLabel;
    const int dim = paramDimension(p->kind);
    ui.defaults.resize(dim > 0 ? (size_t)dim : 0);
    for (int i = 0; i < dim; ++i) ui.defaults[i] = dprop(p->props, kOfxParamPropDefault, i, 0);
    if (paramIsString(p->kind)) ui.defaultString = sprop(p->props, kOfxParamPropDefault);
    ui.choiceLabels.clear();
    ui.choicePtrs.clear();
    if (p->kind == ParamType::Choice) {
      auto it = p->props.m.find(kOfxParamPropChoiceOption);
      if (it != p->props.m.end())
        for (const Val &v : it->second) ui.choiceLabels.push_back(v.s);
      ui.choicePtrs.reserve(ui.choiceLabels.size());
      for (const std::string &s : ui.choiceLabels) ui.choicePtrs.push_back(s.c_str());
    }
    if (p->kind == ParamType::Double || p->kind == ParamType::Integer) {
      double lo = dprop(p->props, kOfxParamPropDisplayMin, 0, dprop(p->props, kOfxParamPropMin, 0, 0));
      double hi = dprop(p->props, kOfxParamPropDisplayMax, 0,
                        dprop(p->props, kOfxParamPropMax, 0, p->kind == ParamType::Integer ? 100 : 1));
      if (!(std::fabs(lo) < 1e7)) lo = 0;
      if (!(std::fabs(hi) < 1e7) || hi <= lo) hi = lo + (p->kind == ParamType::Integer ? 100 : 1);
      ui.displayMin = lo;
      ui.displayMax = hi;
      ui.hardMin = dprop(p->props, kOfxParamPropMin, 0, lo);
      ui.hardMax = dprop(p->props, kOfxParamPropMax, 0, hi);
      ui.step = p->kind == ParamType::Integer ? 1.0 : std::max((hi - lo) / 100.0, 1e-6);
    }
  }
}
static Clip *findClip(Effect *e, const char *name) {
  for (auto &c : e->clips)
    if (c->name == name) return c.get();
  return nullptr;
}

std::unique_ptr<Effect> cloneEffect(const Effect &src) {
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
  p->kind = paramTypeFromString(type);
  p->name = name;
  OfxPropertySetHandle h = H(&p->props);
  propSetString(h, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(h, kOfxParamPropType, 0, type);
  propSetString(h, kOfxPropName, 0, name);
  propSetString(h, kOfxPropLabel, 0, name);
  propSetString(h, kOfxParamPropParent, 0, "");
  propSetInt(h, kOfxParamPropEnabled, 0, 1);
  propSetInt(h, kOfxParamPropSecret, 0, 0);
  for (int i = 0; i < paramDimension(p->kind); ++i) propSetDouble(h, kOfxParamPropDefault, i, 0);
  if (paramIsString(p->kind)) propSetString(h, kOfxParamPropDefault, 0, "");
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
  if (paramIsString(p->kind)) {
    *va_arg(ap, const char **) = p->s.c_str();
    return kOfxStatOK;
  }
  const bool ints = paramIsInteger(p->kind);
  for (double v : p->v) {
    if (ints) *va_arg(ap, int *) = (int)v;
    else *va_arg(ap, double *) = v;
  }
  return kOfxStatOK;
}
static OfxStatus setValue(Param *p, va_list ap) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (paramIsString(p->kind)) {
    const char *s = va_arg(ap, const char *);
    p->s = s ? s : "";
    return kOfxStatOK;
  }
  const bool ints = paramIsInteger(p->kind);
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
  const bool isOutput = c->name == kOfxImageEffectOutputClipName;
  OfxMetalBuffer *mbuf =
      e->metalEnabled ? (isOutput ? (OfxMetalBuffer *)e->curDstMtl : (OfxMetalBuffer *)e->curSrcMtl) : nullptr;
  void *data = mbuf ? ofxMetalBufferHandle(mbuf) : (isOutput ? (void *)e->dst : (void *)e->src);
  if (!data) return kOfxStatFailed;
  // Reuse the clip's pooled PropSet instead of heap-allocating per request.
  c->imgProps.m.clear();
  OfxPropertySetHandle h = H(&c->imgProps);
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
  int bw = 0, bh = 0;
  {
    std::lock_guard<std::mutex> lock(e->dimMutex);
    bw = isOutput ? e->outW : e->w;
    bh = isOutput ? e->outH : e->h;
  }
  const int bounds[4] = {0, 0, bw, bh};
  propSetN<int, propSetInt>(h, kOfxImagePropBounds, 4, bounds);
  propSetN<int, propSetInt>(h, kOfxImagePropRegionOfDefinition, 4, bounds);
  propSetInt(h, kOfxImagePropRowBytes, 0, bounds[2] * 4 * (int)sizeof(float));
  *out = h;
  return kOfxStatOK;
}
static OfxStatus clipReleaseImage(OfxPropertySetHandle) {
  // PropSet is owned by the Clip; cleared on next clipGetImage.
  return kOfxStatOK;
}
static OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle c, OfxTime, OfxRectD *rod) {
  Clip *clip = C(c);
  const bool isOutput = clip->name == kOfxImageEffectOutputClipName;
  Effect *e = clip->owner;
  std::lock_guard<std::mutex> lock(e->dimMutex);
  *rod = {0, 0, (double)(isOutput ? e->outW : e->w), (double)(isOutput ? e->outH : e->h)};
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
  propSetInt(h, kOfxPropAPIVersion, 0, 1);
  propSetInt(h, kOfxPropAPIVersion, 1, 4);
  propSetInt(h, kOfxPropVersion, 0, 0);
  propSetInt(h, kOfxPropVersion, 1, 3);
  propSetInt(h, kOfxPropVersion, 2, 14);
  propSetString(h, kOfxPropVersionLabel, 0, "0.3.14");
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
  // OFX Support plugins throw HostInadequate if these are absent (even when unsupported).
  propSetString(h, kOfxImageEffectPropOpenGLRenderSupported, 0, "false");
  propSetString(h, kOfxImageEffectPropCudaRenderSupported, 0, "false");
  propSetString(h, kOfxImageEffectPropCudaStreamSupported, 0, "false");
#if defined(__APPLE__)
  propSetString(h, kOfxImageEffectPropMetalRenderSupported, 0, "true");
  propSetString(h, kOfxImageEffectPropCPURenderSupported, 0, "true");
#else
  propSetString(h, kOfxImageEffectPropMetalRenderSupported, 0, "false");
#endif
  propSetString(h, kOfxImageEffectPropOpenCLRenderSupported, 0, "false");
  propSetString(h, kOfxImageEffectHostPropNativeOrigin, 0, kOfxHostNativeOriginBottomLeft);
  propSetInt(h, kOfxParamHostPropSupportsCustomInteract, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsStringAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsChoiceAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsBooleanAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsCustomAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropMaxParameters, 0, -1);
  propSetInt(h, kOfxParamHostPropMaxPages, 0, 0);
  propSetInt(h, kOfxParamHostPropPageRowColumnCount, 0, 0);
  propSetInt(h, kOfxParamHostPropPageRowColumnCount, 1, 0);
  propSetInt(h, kOfxImageEffectInstancePropSequentialRender, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsStrChoice, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsStrChoiceAnimation, 0, 0);
  propSetInt(h, kOfxParamHostPropSupportsParametricAnimation, 0, 0);
  propSetInt(h, kOfxImageEffectPropRenderQualityDraft, 0, 0);
  propSetString(h, kOfxImageEffectPropOpenCLSupported, 0, "false");
  return ps;
}();
OfxHost gOfxHost = {H(&gHostProps), fetchSuite};

