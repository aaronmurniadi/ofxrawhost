// OpenFX host property store and the helpers that read and write it. This is the
// lowest layer of the OFX module; the suites and the instance model build on it.
#pragma once

#include "ofxCore.h"

#include <algorithm>
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
