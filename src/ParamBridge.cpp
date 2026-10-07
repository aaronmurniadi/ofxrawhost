#include "ParamBridge.h"

#include "imgio/ImageIO.h"
#include "ofx/OfxHost.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"

#include <string>

// The color tag lives on a plugin choice parameter with one of these labels.
static const char kInputColorLabel[] = "Input Color Space";
static const char kOutputColorLabel[] = "Output Color Space";

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
  OfxPlugin *plugin = gPlugins[node.pluginIndex].plugin;
  callAction(plugin, kOfxActionBeginInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionEndInstanceChanged, node.instance.get(), &in);
  // The plugin may have toggled enabled/secret/labels; keep the UI cache in sync.
  refreshParamUiCache(node.instance.get());
}

void applyColorDefaults(App &app, Node &node) {
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->kind != ParamType::Choice) continue;
    const std::string label = sprop(p->props, kOfxPropLabel);
    const char *want = label == kInputColorLabel  ? colorSpaceName(app.doc.inputSpace)
                       : label == kOutputColorLabel ? "sRGB"
                                                    : nullptr;
    if (!want) continue;
    const auto &options = choiceOptions(p);
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i].s != want) continue;
      {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[0] = (double)i;
      }
      notifyChanged(node, p);
      break;
    }
  }
}

void syncOutputTag(App &app) {
  std::lock_guard<std::mutex> lock(gValueMutex);  // p->v may be written by an in-flight render
  for (int n = (int)app.chain.nodes.size() - 1; n >= 0; --n) {
    for (auto &up : app.chain.nodes[n].instance->params) {
      Param *p = up.get();
      if (p->kind != ParamType::Choice || sprop(p->props, kOfxPropLabel) != kOutputColorLabel ||
          dprop(p->props, kOfxParamPropSecret, 0, 0) != 0)
        continue;
      const auto &options = choiceOptions(p);
      const size_t index = (size_t)p->v[0];
      if (index < options.size()) {
        for (int i = 0; i < kOutputSpaceCount; ++i)
          if (options[index].s == kOutputSpaces[i]) {
            app.outputTag = outputSpace(i);
            return;
          }
      }
    }
  }
}
