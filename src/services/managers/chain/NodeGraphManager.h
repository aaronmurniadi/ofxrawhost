// Chain node graph use-cases: add, remove, reorder, enable, clear, and restore.
#pragma once

#include <memory>

struct ManagerSession;
struct Node;
struct PersistChain;

class NodeGraphManager {
 public:
  virtual ~NodeGraphManager() = default;
  virtual Node *selectedNode() = 0;
  virtual bool addNode(int pluginIndex) = 0;
  virtual void destroyNode(int index) = 0;
  virtual void moveNode(int from, int to) = 0;
  virtual void setNodeEnabled(int index, bool enabled) = 0;
  virtual void clearNodes() = 0;
  // Replaces the chain with the persisted one, then syncs color spaces and the
  // render once for the whole restored chain.
  virtual void applyChain(const PersistChain &chain) = 0;
};

std::unique_ptr<NodeGraphManager> createNodeGraphManager(ManagerSession session);
