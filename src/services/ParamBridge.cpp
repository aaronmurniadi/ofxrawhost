#include "services/ParamBridge.h"
#include "core/RenderContext.h"

#include "imgio/ImageIO.h"
#include "imgio/NoiseProfiles.h"
#include "ofx/OfxHost.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <string>

const std::vector<Val> &choiceOptions(Param *p) {
  static const std::vector<Val> none;
  auto it = p->props.m.find(kOfxParamPropChoiceOption);
  if (it != p->props.m.end()) return it->second;
  return none;
}

void notifyChanged(Node &node, Param *p) {
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const double scale[2] = {1, 1};
  propSetString(a, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(a, kOfxPropName, 0, p->name.c_str());
  propSetString(a, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  OfxPlugin *plugin = plugins()[node.pluginIndex].plugin;
  callAction(plugin, kOfxActionBeginInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionEndInstanceChanged, node.instance.get(), &in);
  // The plugin may have toggled enabled/secret/labels; keep the UI cache in sync.
  refreshParamUiCache(node.instance.get());
}

// Writes spaceName into the node parameter that carries the given label, when
// the plugin offers that option. Returns the parameter it changed.
static Param *writeNodeColorParam(Node &node, const char *label, const char *spaceName) {
  if (!node.instance || !spaceName) return nullptr;
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->kind != ParamType::Choice || sprop(p->props, kOfxPropLabel) != label) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    const auto &options = choiceOptions(p);
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i].s != spaceName) continue;
      if (p->v.empty()) return nullptr;
      if ((size_t)p->v[0] == i) return nullptr;  // already there, nothing to notify
      p->v[0] = (double)i;
      return p;
    }
    return nullptr;
  }
  return nullptr;
}

// Reads the node parameter that carries the given label and maps its selected
// option onto a host color space. Returns false when the node has no such
// parameter or the option has no close host tag.
static bool readNodeColorParam(const Node &node, const char *label, ColorSpace &out) {
  if (!node.instance) return false;
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->kind != ParamType::Choice || sprop(p->props, kOfxPropLabel) != label) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    const auto &options = choiceOptions(p);
    if (p->v.empty()) return false;
    const size_t index = (size_t)p->v[0];
    if (index >= options.size()) return false;
    return hostSpaceForPluginName(options[index].s, out);
  }
  return false;
}

void syncChainColorSpaces(Workspace &app) {
  // Walk the chain in order: the first plugin is told the document input space,
  // and every later plugin is told the space the previous plugin declares. The
  // space that leaves the last enabled plugin is what the preview and the export
  // start from.
  ColorSpace carried = app.doc.inputSpace;
  std::vector<Node *> changedNodes;
  std::vector<Param *> changedParams;
  {
    std::lock_guard<std::mutex> lock(renderContext().values());  // p->v may be written by an in-flight render
    for (Node &node : app.chain.nodes) {
      if (!node.enabled || !node.instance) continue;
      if (Param *p = writeNodeColorParam(node, kInputColorSpaceLabel, colorSpaceName(carried))) {
        changedNodes.push_back(&node);
        changedParams.push_back(p);
      }
      if (readNodeColorParam(node, kOutputColorSpaceLabel, carried)) continue;
      // The plugin does not describe its output, so the space it received passes
      // through unchanged.
    }
    app.chainOutputSpace = carried;
  }
  for (size_t i = 0; i < changedParams.size(); ++i) notifyChanged(*changedNodes[i], changedParams[i]);
}

// Writes value into the node parameter that carries the given label, when it is
// a double parameter and the value is not already there. Returns the parameter it
// changed.
static Param *writeNodeDoubleParam(Node &node, const char *label, double value) {
  if (!node.instance) return nullptr;
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->kind != ParamType::Double) continue;
    if (sprop(p->props, kOfxPropLabel) != label) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    if (p->v.empty()) return nullptr;
    if (p->v[0] == value) return nullptr;  // already there, nothing to notify
    p->v[0] = value;
    return p;
  }
  return nullptr;
}

int seedNoiseModelParams(Workspace &app, Node &node) {
  if (!node.instance || !app.doc.meta.hasCamera || app.doc.meta.iso <= 0) return 0;
  NoiseProfile profile;
  if (!noiseProfileLookup(app.doc.meta.cameraMaker, app.doc.meta.cameraModel, app.doc.meta.iso, profile)) return 0;

  std::vector<Param *> changed;
  {
    std::lock_guard<std::mutex> lock(renderContext().values());  // p->v may be read by an in-flight render
    if (Param *p = writeNodeDoubleParam(node, kNoiseSlopeLabel, profile.a)) changed.push_back(p);
    if (Param *p = writeNodeDoubleParam(node, kNoiseOffsetLabel, profile.b)) changed.push_back(p);
  }
  for (Param *p : changed) notifyChanged(node, p);
  return (int)changed.size();
}

int seedNoiseModelParams(Workspace &app) {
  int seeded = 0;
  for (Node &node : app.chain.nodes)
    if (seedNoiseModelParams(app, node) > 0) ++seeded;
  return seeded;
}

// Maps a plug-in output color space option to the host color space that can
// describe it. The host owns four ICC profiles, so several plug-in names share
// one host tag: the Rec.709 gamma variants match sRGB, the P3 variants and
// DCI-P3 match Display P3, and the wide-gamut encodings match Linear Rec.2020.
bool hostSpaceForPluginName(const std::string &name, ColorSpace &out) {
  static const struct {
    const char *pluginName;
    ColorSpace host;
  } kMap[] = {
      {"sRGB", ColorSpace::sRGB},
      {"Rec.709 Gamma 2.2", ColorSpace::sRGB},
      {"Rec.709 Gamma 2.4", ColorSpace::sRGB},
      {"Display P3", ColorSpace::DisplayP3},
      {"DCI-P3", ColorSpace::DisplayP3},
      {"P3-D65 Gamma 2.2", ColorSpace::DisplayP3},
      {"P3-D65 Gamma 2.6", ColorSpace::DisplayP3},
      {"Linear Rec.709", ColorSpace::LinearRec709},
      {"Linear P3-D65", ColorSpace::LinearRec2020},
      {"Linear Rec.2020", ColorSpace::LinearRec2020},
      {"ProPhoto RGB", ColorSpace::LinearRec2020},
      {"Adobe RGB (1998)", ColorSpace::LinearRec2020},
  };
  for (const auto &entry : kMap) {
    if (name != entry.pluginName) continue;
    out = entry.host;
    return true;
  }
  return false;
}

bool chainHasEnabledNode(Workspace &app) {
  for (const Node &node : app.chain.nodes)
    if (node.enabled && node.instance) return true;
  return false;
}
