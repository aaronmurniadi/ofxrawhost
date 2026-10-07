#include "persist/ChainIO.h"

#include "NodeGraph.h"
#include "ParamBridge.h"
#include "RenderScheduler.h"
#include "ofx/OfxHost.h"
#include "ofxParam.h"

#include <cctype>
#include <sstream>

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
  if (t == kOfxParamTypeBoolean) {
    if (p->v[0] != 0) return "true";
    return "false";
  }
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
    if (v == "true" || v == "1") p->v[0] = 1.0;
    else p->v[0] = 0.0;
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

PersistChain captureChain(const App &app) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  PersistChain chain;
  chain.selectedNode = app.chain.selectedNode;
  for (const Node &n : app.chain.nodes) {
    PersistNode pn;
    OfxPlugin *pl = gPlugins[n.pluginIndex].plugin;
    if (pl && pl->pluginIdentifier) pn.pluginIdentifier = pl->pluginIdentifier;
    else pn.pluginIdentifier = "";
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
    Node &node = app.chain.nodes.back();
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
  if (chain.selectedNode >= 0 && chain.selectedNode < (int)app.chain.nodes.size())
    app.chain.selectedNode = chain.selectedNode;
  else if (!app.chain.nodes.empty())
    app.chain.selectedNode = 0;
  syncOutputTag(app);
  scheduleRender(app);
}
