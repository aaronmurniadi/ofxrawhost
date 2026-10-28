#include "services/managers/chain/NodeGraphManager.h"

#include "services/ManagerSession.h"
#include "services/NodeGraph.h"

namespace {

class NodeGraphManagerImpl : public NodeGraphManager {
 public:
  explicit NodeGraphManagerImpl(ManagerSession session) : session_(session) {}

  Node *selectedNode() override { return ::selectedNode(session_); }

  bool addNode(int pluginIndex) override { return ::addNode(session_, pluginIndex); }

  void destroyNode(int index) override { ::destroyNode(session_, index); }

  void moveNode(int from, int to) override { ::moveNode(session_, from, to); }

  void setNodeEnabled(int index, bool enabled) override { ::setNodeEnabled(session_, index, enabled); }

  void clearNodes() override { ::clearNodes(session_); }

  void applyChain(const PersistChain &chain) override { ::applyChain(session_, chain); }

 private:
  ManagerSession session_;
};

}  // namespace

std::unique_ptr<NodeGraphManager> createNodeGraphManager(ManagerSession session) {
  return std::make_unique<NodeGraphManagerImpl>(session);
}
