// The OFX property suite: the key/value store plugins read and write.
//
// Split out of the single suite file, because a plugin bug report names one
// suite and the reader should land on it.

#include "ofx/OfxSuitesPriv.h"

#include "ofx/OfxProps.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
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
  if (v) slot(h, k, i)->s = v;
  else slot(h, k, i)->s.clear();
  return kOfxStatOK;
}
OfxStatus propSetDouble(OfxPropertySetHandle h, const char *k, int i, double v) {
  CHECK_SET(h, i);
  Val *s = slot(h, k, i);
  s->d = v;
  if (std::isfinite(v)) s->i = (int)std::lround(std::clamp(v, (double)INT_MIN, (double)INT_MAX));
  else s->i = 0;
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
  if (it == P(h)->m.end()) *count = 0;
  else *count = (int)it->second.size();
  return kOfxStatOK;
}

std::string sprop(const PropSet &ps, const char *k, int i) {
  auto it = ps.m.find(k);
  if (it == ps.m.end()) return "";
  if (i >= (int)it->second.size()) return "";
  return it->second[i].s;
}
double dprop(const PropSet &ps, const char *k, int i, double fallback) {
  auto it = ps.m.find(k);
  if (it == ps.m.end()) return fallback;
  if (i >= (int)it->second.size()) return fallback;
  return it->second[i].d;
}


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

const OfxPropertySuiteV1 &propSuiteTable() { return gPropSuite; }
