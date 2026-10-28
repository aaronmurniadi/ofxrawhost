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

Node *selectedNode(ManagerSession session) {
  if (session.chain.selectedNode < 0 || session.chain.selectedNode >= (int)session.chain.nodes.size()) return nullptr;
  return &session.chain.nodes[session.chain.selectedNode];
}

void destroyNode(ManagerSession session, int index) {
  if (index < 0 || index >= (int)session.chain.nodes.size()) return;
  session.render.waitIdle();
  Node &n = session.chain.nodes[index];
  if (n.instance) callAction(plugins()[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  session.chain.nodes.erase(session.chain.nodes.begin() + index);
  if (session.chain.nodes.empty())
    session.chain.selectedNode = -1;
  else if (session.chain.selectedNode >= (int)session.chain.nodes.size())
    session.chain.selectedNode = (int)session.chain.nodes.size() - 1;
  else if (session.chain.selectedNode > index)
    --session.chain.selectedNode;
  session.gui.paramFilter[0] = '\0';
  session.scheduleRender();
}

void clearNodes(ManagerSession session) {
  session.render.waitIdle();
  for (auto &n : session.chain.nodes)
    if (n.instance) callAction(plugins()[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  session.chain.nodes.clear();
  session.chain.selectedNode = -1;
  session.gui.paramFilter[0] = '\0';
}

// Creates the instance and appends it, leaving the chain color spaces and the
// render schedule to the caller: addNode does both for one node, and applyChain
// does both once, after every node of the restored chain is in place.
static bool appendNode(ManagerSession session, int pluginIndex) {
  if (pluginIndex < 0 || pluginIndex >= (int)plugins().size()) return false;
  session.render.waitIdle();
  Node node;
  node.pluginIndex = pluginIndex;
  node.instance = createInstance(plugins()[pluginIndex]);
  if (!node.instance) {
    session.reportStatus("Plugin failed to create an instance");
    return false;
  }
  if (session.document.preview.w) node.instance->setInputSize(session.document.preview.w, session.document.preview.h);
  for (auto &p : node.instance->params)
    if (p->type == kOfxParamTypeGroup) node.groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  session.chain.nodes.push_back(std::move(node));
  session.chain.selectedNode = (int)session.chain.nodes.size() - 1;
  session.gui.paramFilter[0] = '\0';
  // A new node takes the noise model of the camera the image came from, unless
  // the plugin is added for another reason and has no such parameters.
  seedNoiseModelParams(session, session.chain.nodes.back());
  return true;
}

bool addNode(ManagerSession session, int pluginIndex) {
  if (!appendNode(session, pluginIndex)) return false;
  // Walk after the push, so the new node receives the space the chain carries.
  syncChainColorSpaces(session);
  session.scheduleRender();
  return true;
}

void applyChain(ManagerSession session, const PersistChain &chain) {
  clearNodes(session);
  for (const PersistNode &pn : chain.nodes) {
    const int pi = findPluginIndex(pn.pluginIdentifier, pn.pluginLabel);
    if (pi < 0 || !appendNode(session, pi)) continue;
    Node &node = session.chain.nodes.back();
    node.enabled = pn.enabled;
    node.groupOpen = pn.groupOpen;
    restoreNodeParams(node, pn);
  }
  if (chain.selectedNode >= 0 && chain.selectedNode < (int)session.chain.nodes.size())
    session.chain.selectedNode = chain.selectedNode;
  else if (!session.chain.nodes.empty())
    session.chain.selectedNode = 0;
  // Once, for the whole restored chain: while it is being built the chain is
  // transient, so a walk of every prefix and a render per node would be work
  // thrown away.
  syncChainColorSpaces(session);
  session.scheduleRender();
}

void moveNode(ManagerSession session, int from, int to) {
  if (from < 0 || to < 0 || from >= (int)session.chain.nodes.size() || to >= (int)session.chain.nodes.size() || from == to) return;
  session.render.waitIdle();
  Node n = std::move(session.chain.nodes[from]);
  session.chain.nodes.erase(session.chain.nodes.begin() + from);
  session.chain.nodes.insert(session.chain.nodes.begin() + to, std::move(n));
  session.chain.selectedNode = to;
  syncChainColorSpaces(session);
  session.scheduleRender();
}

void setNodeEnabled(ManagerSession session, int index, bool enabled) {
  if (index < 0 || index >= (int)session.chain.nodes.size()) return;
  session.render.waitIdle();  // the worker reads node.enabled
  session.chain.nodes[index].enabled = enabled;
  session.scheduleRender();
}
