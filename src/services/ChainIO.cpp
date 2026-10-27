#include "services/ChainIO.h"
#include "core/RenderContext.h"

#include "ofx/OfxHost.h"
#include "ofxParam.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

// One parameter value as JSON. Strings, booleans, scalars and n-dimensional
// vectors all land in the JsonValue the project writer already uses, so this
// module no longer serializes by hand.
static JsonValue paramValueJson(Param *p) {
  if (paramIsString(p->kind)) return JsonValue::makeString(p->s);
  if (p->kind == ParamType::Boolean) return JsonValue::makeBool(p->v[0] != 0);
  const int count = paramDimension(p->kind);
  if (count > 1) {
    JsonValue array = JsonValue::makeArray();
    for (int i = 0; i < count; ++i) array.arr.push_back(JsonValue::makeDouble(p->v[i]));
    return array;
  }
  return JsonValue::makeDouble(p->v[0]);
}

// A number from any numeric JSON value, so a value saved as an integer still
// loads into a double parameter.
static double jsonNumber(const JsonValue &value, double fallback) {
  if (value.kind == JsonValue::Kind::Double) return value.d;
  if (value.kind == JsonValue::Kind::Int) return (double)value.i;
  if (value.kind == JsonValue::Kind::Bool) {
    if (value.b) return 1.0;
    return 0.0;
  }
  return fallback;
}

static void applyParamValueJson(Param *p, const JsonValue &value) {
  std::lock_guard<std::mutex> lock(renderContext().values());
  if (paramIsString(p->kind)) {
    if (value.kind == JsonValue::Kind::String) p->s = value.s;
    return;
  }
  if (p->kind == ParamType::Boolean) {
    const double on = 1.0;
    const double off = 0.0;
    if (value.kind == JsonValue::Kind::Bool) {
      if (value.b) p->v[0] = on;
      else p->v[0] = off;
    } else {
      p->v[0] = jsonNumber(value, p->v[0]);
    }
    return;
  }
  const int count = paramDimension(p->kind);
  if (count > 1) {
    if (value.kind != JsonValue::Kind::Array) return;
    const size_t available = value.arr.size();
    for (int i = 0; i < count && (size_t)i < available; ++i) p->v[i] = jsonNumber(value.arr[i], p->v[i]);
    return;
  }
  p->v[0] = jsonNumber(value, p->v[0]);
}

int findPluginIndex(const std::string &identifier, const std::string &labelFallback) {
  if (!identifier.empty()) {
    for (int i = 0; i < (int)plugins().size(); ++i) {
      OfxPlugin *p = plugins()[i].plugin;
      if (p && p->pluginIdentifier && identifier == p->pluginIdentifier) return i;
    }
  }
  if (!labelFallback.empty()) {
    for (int i = 0; i < (int)plugins().size(); ++i)
      if (plugins()[i].label == labelFallback) return i;
  }
  return -1;
}

// The bundled Transform plugin merged its Crop and Zoom parameters into one
// Zoom parameter. A project saved before the merge stores a "crop" key, so
// fold both old values into the merged Zoom and keep the field of view the
// node showed. New projects have no "crop" key and are left untouched. A
// plugin that still declares Crop keeps its own value.
static void migrateMergedTransformParams(const PersistNode &pn, Effect *effect) {
  if (pn.pluginIdentifier != "com.aaronmurniadi.ofxrawhost.transform") return;
  auto cropIt = pn.params.find("crop");
  if (cropIt == pn.params.end()) return;
  if (findParam(effect, "crop")) return;
  Param *zoom = findParam(effect, "zoom");
  if (!zoom || zoom->v.empty()) return;
  const double cropVal = jsonNumber(cropIt->second, 0.0);
  const double scale = std::max(1.0 - cropVal / 100.0, 0.02);
  std::lock_guard<std::mutex> lock(renderContext().values());
  zoom->v[0] = std::clamp(zoom->v[0] / scale, 100.0, 1000.0);
}

PersistChain captureChain(const ChainState &state) {
  std::lock_guard<std::mutex> lock(renderContext().values());
  PersistChain chain;
  chain.selectedNode = state.selectedNode;
  for (const Node &n : state.nodes) {
    PersistNode pn;
    OfxPlugin *pl = plugins()[n.pluginIndex].plugin;
    if (pl && pl->pluginIdentifier) pn.pluginIdentifier = pl->pluginIdentifier;
    else pn.pluginIdentifier = "";
    pn.pluginLabel = plugins()[n.pluginIndex].label;
    pn.enabled = n.enabled;
    pn.groupOpen = n.groupOpen;
    if (n.instance) {
      for (const auto &up : n.instance->params) {
        Param *p = up.get();
        if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
        if (p->type == kOfxParamTypeGroup || p->type == kOfxParamTypePage || p->type == kOfxParamTypePushButton) continue;
        pn.params[p->name] = paramValueJson(p);
      }
    }
    chain.nodes.push_back(std::move(pn));
  }
  return chain;
}

void restoreNodeParams(Node &node, const PersistNode &pn) {
  if (!node.instance) return;
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    auto it = pn.params.find(p->name);
    if (it != pn.params.end()) applyParamValueJson(p, it->second);
  }
  migrateMergedTransformParams(pn, node.instance.get());
}
