// OpenFX host side: property store, suites, plugin loading and CPU rendering.
#pragma once

#include "ofxCore.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Val {
  std::string s;
  double d = 0;
  int i = 0;
  void *p = nullptr;
};
struct PropSet {
  std::unordered_map<std::string, std::vector<Val>> m;
};

inline PropSet *P(OfxPropertySetHandle h) { return reinterpret_cast<PropSet *>(h); }
inline OfxPropertySetHandle H(PropSet *p) { return reinterpret_cast<OfxPropertySetHandle>(p); }

OfxStatus propSetPointer(OfxPropertySetHandle h, const char *k, int i, void *v);
OfxStatus propSetString(OfxPropertySetHandle h, const char *k, int i, const char *v);
OfxStatus propSetDouble(OfxPropertySetHandle h, const char *k, int i, double v);
OfxStatus propSetInt(OfxPropertySetHandle h, const char *k, int i, int v);
template <class T, OfxStatus (*F)(OfxPropertySetHandle, const char *, int, T)>
OfxStatus propSetN(OfxPropertySetHandle h, const char *k, int n, const T *v) {
  if (!h) return kOfxStatErrBadHandle;
  P(h)->m[k].resize(std::max(n, 0));
  for (int i = 0; i < n; ++i) F(h, k, i, v[i]);
  return kOfxStatOK;
}
std::string sprop(const PropSet &ps, const char *k, int i = 0);
double dprop(const PropSet &ps, const char *k, int i, double fallback);

// Parameter kind, resolved once from the kOfxParamType* string. The string stays
// on Param for the OFX suites. The enum answers shape questions without a compare.
enum class ParamType {
  Unknown,
  Integer, Double, Boolean, Choice, String, Custom, StrChoice, Bytes,
  PushButton, Group, Page,
  Integer2D, Double2D, Integer3D, Double3D, RGB, RGBA,
};

ParamType paramTypeFromString(const std::string &type);
int paramDimension(ParamType t);
bool paramIsInteger(ParamType t);
bool paramIsString(ParamType t);

// UI metadata cached once per instance. The parameter panel rebuilds strings and
// re-scans the property map every frame from Param::props; this holds the result.
// Refresh it after a plugin may have changed its own properties (instance-changed).
struct ParamUiCache {
  std::string label, hint, parent, idLabel;
  std::vector<std::string> choiceLabels;
  std::vector<const char *> choicePtrs;
  std::vector<double> defaults;
  std::string defaultString;
  double displayMin = 0, displayMax = 0, hardMin = 0, hardMax = 0, step = 1;
  bool enabled = true, secret = false, stringIsLabel = false;
};
struct Param {
  std::string type, name;
  ParamType kind = ParamType::Unknown;
  PropSet props;
  std::vector<double> v;
  std::string s;
  ParamUiCache ui;
};
struct Effect;
struct Clip {
  std::string name;
  PropSet props;
  PropSet imgProps;  // reusable buffer for clipGetImage (avoids new/delete per request)
  Effect *owner = nullptr;
};
struct Effect {
  PropSet props, paramSetProps;
  std::vector<std::unique_ptr<Param>> params;
  std::vector<std::unique_ptr<Clip>> clips;
  float *src = nullptr, *dst = nullptr;
  void *srcMtl = nullptr, *dstMtl = nullptr;  // owned scratch id<MTLBuffer>s when metalEnabled
  size_t srcMtlBytes = 0, dstMtlBytes = 0;    // capacities of the two owned buffers
  void *curSrcMtl = nullptr, *curDstMtl = nullptr;  // buffers for the in-flight render (may be chain-owned)
  bool metalEnabled = false;                  // this render passes MTLBuffer images
  bool metalCapable = false;                  // plugin declared kOfxImageEffectPropMetalRenderSupported
  int w = 0, h = 0;                           // input/source clip dims
  int outW = 0, outH = 0;                     // output clip dims (== w,h unless the plugin changes its RoD)
  int renderGen = 0;
  std::mutex dimMutex;                        // guards w/h/outW/outH: renders write them, suite actions read them

  // Sets the input clip size under dimMutex. The render path owns outW and outH.
  void setInputSize(int width, int height);

  // Releases the Metal scratch buffers, if any.
  ~Effect();
};

// Bumping gLatestGen aborts in-flight interactive renders. gValueMutex guards Param values.
extern std::atomic<int> gLatestGen;
extern std::mutex gValueMutex;
// Called with plugin warnings/errors, on the thread that raised them.
extern std::function<void(const std::string &)> gOnMessage;

int dims(const std::string &type);
bool isIntType(const std::string &type);
Param *findParam(Effect *e, const char *name);
// Rebuilds every Param::ui in the effect from the current property values.
void refreshParamUiCache(Effect *e);

struct PluginEntry {
  OfxPlugin *plugin;
  std::string label;
  std::string author;
  std::unique_ptr<Effect> descriptor;  // filter-context descriptor
  bool metalCapable = false;           // plugin declared kOfxImageEffectPropMetalRenderSupported
};
extern std::vector<PluginEntry> gPlugins;

OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in = nullptr, PropSet *out = nullptr);
void loadPlugins();
std::unique_ptr<Effect> createInstance(PluginEntry &pe);
// Output size the plugin declares for input size inW×inH (kOfxImageEffectActionGetRegionOfDefinition).
// Falls back to inW×inH when the plugin does not override its RoD.
void queryOutputSize(OfxPlugin *p, Effect *e, int inW, int inH, int *outW, int *outH);
// src: bottom-up float RGBA w*h pixels. dst receives outW*outH pixels (capacity >= outW*outH).
// gen 0 = never aborted.
// srcMtl/dstMtl are optional id<MTLBuffer> handles for chained GPU renders. When a
// handle is given, the image lives on the GPU and the matching CPU pointer may be
// null. When dstMtl is null and the node renders on Metal, renderEffect syncs and
// copies the result back to dst, so single-node callers keep the old behavior. The
// caller owns the sync when dstMtl is given, and must call ofxMetalSync() before it
// reads that buffer on the CPU.
// draft selects kOfxImageEffectPropRenderQualityDraft for reduced-quality interactive passes.
OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int outW, int outH, int gen,
                       void *srcMtl = nullptr, void *dstMtl = nullptr, bool draft = false);

// True when the node renders through Metal on this machine.
bool effectUsesMetal(const Effect *e);
