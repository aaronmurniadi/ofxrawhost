#include "NodeGraph.h"

#include "RenderPipeline.h"
#include "ofxParam.h"

#include <cctype>
#include <sstream>

Node *selectedNode(App &app) {
  if (app.selectedNode < 0 || app.selectedNode >= (int)app.nodes.size()) return nullptr;
  return &app.nodes[app.selectedNode];
}

static int findPluginIndex(const std::string &identifier, const std::string &labelFallback) {
  if (!identifier.empty()) {
    for (int i = 0; i < (int)gPlugins.size(); ++i) {
      OfxPlugin *p = gPlugins[i].plugin;
      if (p && p->pluginIdentifier && identifier == p->pluginIdentifier) return i;
    }
  }
  if (!labelFallback.empty()) {
    for (int i = 0; i < (int)gPlugins.size(); ++i)
      if (gPlugins[i].label == labelFallback) return i;
  }
  return -1;
}

static std::string paramValueJson(Param *p) {
  const std::string &t = p->type;
  if (t == kOfxParamTypeString || t == kOfxParamTypeCustom) {
    std::string s = p->s;
    std::string esc;
    esc.reserve(s.size() + 4);
    for (char c : s) {
      if (c == '"' || c == '\\') esc += '\\';
      esc += c;
    }
    return std::string("\"") + esc + '"';
  }
  if (t == kOfxParamTypeBoolean) return p->v[0] != 0 ? "true" : "false";
  const int d = dims(t);
  if (d > 1) {
    std::ostringstream o;
    o << '[';
    for (int i = 0; i < d; ++i) {
      if (i) o << ',';
      o << p->v[i];
    }
    o << ']';
    return o.str();
  }
  return std::to_string(p->v[0]);
}

static void applyParamValueJson(Param *p, const std::string &raw) {
  const std::string &t = p->type;
  std::string v = raw;
  while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(v.begin());
  while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (t == kOfxParamTypeString || t == kOfxParamTypeCustom) {
    if (v.size() >= 2 && v.front() == '"') {
      std::string s;
      for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
          s += v[++i];
          continue;
        }
        if (v[i] == '"') break;
        s += v[i];
      }
      p->s = s;
    }
    return;
  }
  if (t == kOfxParamTypeBoolean) {
    p->v[0] = (v == "true" || v == "1") ? 1.0 : 0.0;
    return;
  }
  const int d = dims(t);
  if (d > 1 && !v.empty() && v.front() == '[') {
    size_t i = 1;
    for (int dim = 0; dim < d && i < v.size(); ++dim) {
      while (i < v.size() && (std::isspace((unsigned char)v[i]) || v[i] == ',')) ++i;
      char *end = nullptr;
      p->v[dim] = std::strtod(v.c_str() + i, &end);
      if (end) i = (size_t)(end - v.c_str());
    }
    return;
  }
  p->v[0] = std::strtod(v.c_str(), nullptr);
}

const std::vector<Val> &choiceOptions(Param *p) {
  static const std::vector<Val> none;
  auto it = p->props.m.find(kOfxParamPropChoiceOption);
  return it != p->props.m.end() ? it->second : none;
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
}

void applyColorDefaults(App &app, Node &node) {
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice) continue;
    const std::string label = sprop(p->props, kOfxPropLabel);
    const char *want =
        label == "Input Color Space" ? colorSpaceName(app.inputSpace) : label == "Output Color Space" ? "sRGB" : nullptr;
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
  for (int n = (int)app.nodes.size() - 1; n >= 0; --n) {
    for (auto &up : app.nodes[n].instance->params) {
      Param *p = up.get();
      if (p->type != kOfxParamTypeChoice || sprop(p->props, kOfxPropLabel) != "Output Color Space" ||
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

void destroyNode(App &app, int index) {
  if (index < 0 || index >= (int)app.nodes.size()) return;
  waitRenderIdle(app);
  Node &n = app.nodes[index];
  if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.nodes.erase(app.nodes.begin() + index);
  if (app.nodes.empty())
    app.selectedNode = -1;
  else if (app.selectedNode >= (int)app.nodes.size())
    app.selectedNode = (int)app.nodes.size() - 1;
  else if (app.selectedNode > index)
    --app.selectedNode;
  app.paramFilter[0] = '\0';
  scheduleRender(app);
}

void clearNodes(App &app) {
  waitRenderIdle(app);
  for (auto &n : app.nodes)
    if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.nodes.clear();
  app.selectedNode = -1;
  app.paramFilter[0] = '\0';
}

bool addNode(App &app, int pluginIndex) {
  if (pluginIndex < 0 || pluginIndex >= (int)gPlugins.size()) return false;
  waitRenderIdle(app);
  Node node;
  node.pluginIndex = pluginIndex;
  node.instance = createInstance(gPlugins[pluginIndex]);
  if (!node.instance) {
    app.setStatus("Plugin failed to create an instance");
    return false;
  }
  if (app.preview.w) {
    node.instance->w = app.preview.w;
    node.instance->h = app.preview.h;
  }
  applyColorDefaults(app, node);
  for (auto &p : node.instance->params)
    if (p->type == kOfxParamTypeGroup) node.groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  app.nodes.push_back(std::move(node));
  app.selectedNode = (int)app.nodes.size() - 1;
  app.paramFilter[0] = '\0';
  syncOutputTag(app);
  scheduleRender(app);
  return true;
}

void moveNode(App &app, int from, int to) {
  if (from < 0 || to < 0 || from >= (int)app.nodes.size() || to >= (int)app.nodes.size() || from == to) return;
  waitRenderIdle(app);
  Node n = std::move(app.nodes[from]);
  app.nodes.erase(app.nodes.begin() + from);
  app.nodes.insert(app.nodes.begin() + to, std::move(n));
  app.selectedNode = to;
  syncOutputTag(app);
  scheduleRender(app);
}

PersistChain captureChain(const App &app) {
  PersistChain chain;
  chain.selectedNode = app.selectedNode;
  for (const Node &n : app.nodes) {
    PersistNode pn;
    OfxPlugin *pl = gPlugins[n.pluginIndex].plugin;
    pn.pluginIdentifier = pl && pl->pluginIdentifier ? pl->pluginIdentifier : "";
    pn.pluginLabel = gPlugins[n.pluginIndex].label;
    pn.enabled = n.enabled;
    pn.groupOpen = n.groupOpen;
    if (n.instance) {
      for (const auto &up : n.instance->params) {
        Param *p = up.get();
        if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
        if (p->type == kOfxParamTypeGroup || p->type == kOfxParamTypePage || p->type == kOfxParamTypePushButton) continue;
        pn.paramsJson[p->name] = paramValueJson(p);
      }
    }
    chain.nodes.push_back(std::move(pn));
  }
  return chain;
}

void applyChain(App &app, const PersistChain &chain) {
  clearNodes(app);
  for (const PersistNode &pn : chain.nodes) {
    const int pi = findPluginIndex(pn.pluginIdentifier, pn.pluginLabel);
    if (pi < 0 || !addNode(app, pi)) continue;
    Node &node = app.nodes.back();
    node.enabled = pn.enabled;
    node.groupOpen = pn.groupOpen;
    if (node.instance) {
      for (auto &up : node.instance->params) {
        Param *p = up.get();
        auto it = pn.paramsJson.find(p->name);
        if (it != pn.paramsJson.end()) applyParamValueJson(p, it->second);
      }
    }
  }
  if (chain.selectedNode >= 0 && chain.selectedNode < (int)app.nodes.size())
    app.selectedNode = chain.selectedNode;
  else if (!app.nodes.empty())
    app.selectedNode = 0;
  syncOutputTag(app);
  scheduleRender(app);
}
