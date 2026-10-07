#include "NodeGraph.h"

#include "ParamBridge.h"
#include "RenderScheduler.h"
#include "ofx/OfxHost.h"
#include "ofxParam.h"

// Out-of-line Node special members: Chain.h forward-declares Effect, so the
// unique_ptr destructor needs a translation unit where Effect is complete.
Node::Node() = default;
Node::~Node() = default;
Node::Node(Node &&) noexcept = default;
Node &Node::operator=(Node &&) noexcept = default;

Node *selectedNode(App &app) {
  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) return nullptr;
  return &app.chain.nodes[app.chain.selectedNode];
}

void destroyNode(App &app, int index) {
  if (index < 0 || index >= (int)app.chain.nodes.size()) return;
  waitRenderIdle(app);
  Node &n = app.chain.nodes[index];
  if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.chain.nodes.erase(app.chain.nodes.begin() + index);
  if (app.chain.nodes.empty())
    app.chain.selectedNode = -1;
  else if (app.chain.selectedNode >= (int)app.chain.nodes.size())
    app.chain.selectedNode = (int)app.chain.nodes.size() - 1;
  else if (app.chain.selectedNode > index)
    --app.chain.selectedNode;
  app.gui.paramFilter[0] = '\0';
  scheduleRender(app);
}

void clearNodes(App &app) {
  waitRenderIdle(app);
  for (auto &n : app.chain.nodes)
    if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.chain.nodes.clear();
  app.chain.selectedNode = -1;
  app.gui.paramFilter[0] = '\0';
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
  if (app.doc.preview.w) node.instance->setInputSize(app.doc.preview.w, app.doc.preview.h);
  applyColorDefaults(app, node);
  for (auto &p : node.instance->params)
    if (p->type == kOfxParamTypeGroup) node.groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  app.chain.nodes.push_back(std::move(node));
  app.chain.selectedNode = (int)app.chain.nodes.size() - 1;
  app.gui.paramFilter[0] = '\0';
  syncOutputTag(app);
  scheduleRender(app);
  return true;
}

void moveNode(App &app, int from, int to) {
  if (from < 0 || to < 0 || from >= (int)app.chain.nodes.size() || to >= (int)app.chain.nodes.size() || from == to) return;
  waitRenderIdle(app);
  Node n = std::move(app.chain.nodes[from]);
  app.chain.nodes.erase(app.chain.nodes.begin() + from);
  app.chain.nodes.insert(app.chain.nodes.begin() + to, std::move(n));
  app.chain.selectedNode = to;
  syncOutputTag(app);
  scheduleRender(app);
}

void setNodeEnabled(App &app, int index, bool enabled) {
  if (index < 0 || index >= (int)app.chain.nodes.size()) return;
  waitRenderIdle(app);  // the worker reads node.enabled
  app.chain.nodes[index].enabled = enabled;
  scheduleRender(app);
}
