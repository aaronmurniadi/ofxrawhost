// OpenFX host instance model: the parameter/clip/effect objects a plugin sees,
// plus the small type helpers the UI and the node graph share. GPU scratch state
// is not part of this interface; it lives behind Effect::gpu (see OfxEffectGpu.h).
#pragma once

#include "ofx/OfxProps.h"
#include "ofxCore.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// Labels of the color-space parameters the host owns and drives. A preset import
// must leave them alone, so the host and the plugin keep the same color spaces.
inline constexpr char kInputColorSpaceLabel[] = "Input Color Space";
inline constexpr char kOutputColorSpaceLabel[] = "Output Color Space";
struct Effect;
struct Clip {
  std::string name;
  PropSet props;
  PropSet imgProps;  // reusable buffer for clipGetImage (avoids new/delete per request)
  Effect *owner = nullptr;
};

// GPU scratch buffers and the per-render Metal flags. Defined in OfxEffectGpu.h
// so the public instance model carries no Metal types.
struct EffectGpu;

struct Effect {
  PropSet props, paramSetProps;
  std::vector<std::unique_ptr<Param>> params;
  std::vector<std::unique_ptr<Clip>> clips;
  float *src = nullptr, *dst = nullptr;
  int w = 0, h = 0;      // input/source clip dims
  int outW = 0, outH = 0;  // output clip dims (== w,h unless the plugin changes its RoD)
  int renderGen = 0;
  std::mutex dimMutex;  // guards w/h/outW/outH: renders write them, suite actions read them
  std::unique_ptr<EffectGpu> gpu;

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
