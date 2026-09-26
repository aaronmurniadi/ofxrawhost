// OpenFX host side: property store, suites, plugin loading and CPU rendering.
#pragma once

#include "ofxImageEffect.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

@class NSString;

struct Val {
  std::string s;
  double d = 0;
  int i = 0;
  void *p = nullptr;
};
struct PropSet {
  std::map<std::string, std::vector<Val>> m;
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

// Bumping gLatestGen aborts in-flight interactive renders. gValueMutex guards Param values.
extern std::atomic<int> gLatestGen;
extern std::mutex gValueMutex;
// Called with plugin warnings/errors, on the thread that raised them.
extern void (^gOnMessage)(NSString *);

int dims(const std::string &type);
bool isIntType(const std::string &type);
Param *findParam(Effect *e, const char *name);

struct PluginEntry {
  OfxPlugin *plugin;
  std::string label;
  std::unique_ptr<Effect> descriptor;  // filter-context descriptor
};
extern std::vector<PluginEntry> gPlugins;

OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in = nullptr);
void loadPlugins();
std::unique_ptr<Effect> createInstance(PluginEntry &pe);
// src/dst: bottom-up float RGBA, w*h pixels. gen 0 = never aborted.
OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int gen);
