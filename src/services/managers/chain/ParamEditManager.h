// Parameter edit use-cases: commit one edited value, and reset a node to its
// plugin defaults while keeping the host color-space parameters.
#pragma once

#include <memory>

struct ManagerSession;
struct Node;
struct Param;

class ParamEditManager {
 public:
  virtual ~ParamEditManager() = default;
  virtual void commit(Node &node, Param *p) = 0;
  virtual int resetToDefaults(Node &node) = 0;
};

std::unique_ptr<ParamEditManager> createParamEditManager(ManagerSession session);
