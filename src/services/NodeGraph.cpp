#include "services/NodeGraph.h"

#include "services/ChainIO.h"
#include "services/ParamBridge.h"
#include "engine/RenderWorker.h"
#include "ofx/OfxHost.h"
#include "ofxParam.h"
#include "persist/ProjectPersist.h"

// Out-of-line Node special members: Chain.h forward-declares Effect, so the
// unique_ptr destructor needs a translation unit where Effect is complete.
Node::Node() = default;
Node::~Node() = default;
Node::Node(Node &&) noexcept = default;
Node &Node::operator=(Node &&) noexcept = default;

Node *selectedNode(Workspace &app) {
  if (app.chain.selectedNode < 0 || app.chain.selectedNode >= (int)app.chain.nodes.size()) return nullptr;
  return &app.chain.nodes[app.chain.selectedNode];
}

void destroyNode(Workspace &app, int index) {
  if (index < 0 || index >= (int)app.chain.nodes.size()) return;
  app.render.waitIdle();
  Node &n = app.chain.nodes[index];
  if (n.instance) callAction(plugins()[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.chain.nodes.erase(app.chain.nodes.begin() + index);
  if (app.chain.nodes.empty())
    app.chain.selectedNode = -1;
  else if (app.chain.selectedNode >= (int)app.chain.nodes.size())
    app.chain.selectedNode = (int)app.chain.nodes.size() - 1;
  else if (app.chain.selectedNode > index)
    --app.chain.selectedNode;
  app.gui.paramFilter[0] = '\0';
  app.scheduleRender();
}

void clearNodes(Workspace &app) {
  app.render.waitIdle();
  for (auto &n : app.chain.nodes)
    if (n.instance) callAction(plugins()[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.chain.nodes.clear();
  app.chain.selectedNode = -1;
  app.gui.paramFilter[0] = '\0';
}

// Creates the instance and appends it, leaving the chain color spaces and the
// render schedule to the caller: addNode does both for one node, and applyChain
// does both once, after every node of the restored chain is in place.
static bool appendNode(Workspace &app, int pluginIndex) {
  if (pluginIndex < 0 || pluginIndex >= (int)plugins().size()) return false;
  app.render.waitIdle();
  Node node;
  node.pluginIndex = pluginIndex;
  node.instance = createInstance(plugins()[pluginIndex]);
  if (!node.instance) {
    app.reportStatus("Plugin failed to create an instance");
    return false;
  }
  if (app.document.preview.w) node.instance->setInputSize(app.document.preview.w, app.document.preview.h);
  for (auto &p : node.instance->params)
    if (p->type == kOfxParamTypeGroup) node.groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  app.chain.nodes.push_back(std::move(node));
  app.chain.selectedNode = (int)app.chain.nodes.size() - 1;
  app.gui.paramFilter[0] = '\0';
  // A new node takes the noise model of the camera the image came from, unless
  // the plugin is added for another reason and has no such parameters.
  seedNoiseModelParams(app, app.chain.nodes.back());
  return true;
}

bool addNode(Workspace &app, int pluginIndex) {
  if (!appendNode(app, pluginIndex)) return false;
  // Walk after the push, so the new node receives the space the chain carries.
  syncChainColorSpaces(app);
  app.scheduleRender();
  return true;
}

void applyChain(Workspace &app, const PersistChain &chain) {
  clearNodes(app);
  for (const PersistNode &pn : chain.nodes) {
    const int pi = findPluginIndex(pn.pluginIdentifier, pn.pluginLabel);
    if (pi < 0 || !appendNode(app, pi)) continue;
    Node &node = app.chain.nodes.back();
    node.enabled = pn.enabled;
    node.groupOpen = pn.groupOpen;
    restoreNodeParams(node, pn);
  }
  if (chain.selectedNode >= 0 && chain.selectedNode < (int)app.chain.nodes.size())
    app.chain.selectedNode = chain.selectedNode;
  else if (!app.chain.nodes.empty())
    app.chain.selectedNode = 0;
  // Once, for the whole restored chain: while it is being built the chain is
  // transient, so a walk of every prefix and a render per node would be work
  // thrown away.
  syncChainColorSpaces(app);
  app.scheduleRender();
}

void moveNode(Workspace &app, int from, int to) {
  if (from < 0 || to < 0 || from >= (int)app.chain.nodes.size() || to >= (int)app.chain.nodes.size() || from == to) return;
  app.render.waitIdle();
  Node n = std::move(app.chain.nodes[from]);
  app.chain.nodes.erase(app.chain.nodes.begin() + from);
  app.chain.nodes.insert(app.chain.nodes.begin() + to, std::move(n));
  app.chain.selectedNode = to;
  syncChainColorSpaces(app);
  app.scheduleRender();
}

void setNodeEnabled(Workspace &app, int index, bool enabled) {
  if (index < 0 || index >= (int)app.chain.nodes.size()) return;
  app.render.waitIdle();  // the worker reads node.enabled
  app.chain.nodes[index].enabled = enabled;
  app.scheduleRender();
}
