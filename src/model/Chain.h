// The plugin node chain: its model and the effect instances it owns. Kept free
// of App so the render engine and the persistence adapter can depend on it
// without depending on the whole application.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward-declared so this header does not need the OFX host.
struct Effect;

struct Node {
  int pluginIndex = -1;
  bool enabled = true;
  std::unique_ptr<Effect> instance;
  std::map<std::string, bool> groupOpen;

  // Declared so the unique_ptr destructor can be defined where Effect is complete.
  Node();
  ~Node();
  Node(Node &&) noexcept;
  Node &operator=(Node &&) noexcept;
};

struct ChainState {
  std::vector<Node> nodes;
  int selectedNode = -1;
};
