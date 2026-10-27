// Adapter between the node chain and the persisted project shape. Pure data in
// both directions: it never creates or destroys an instance and never touches
// rendering, so it needs no App. Creating and destroying nodes lives in
// NodeGraph.h, which is where the effect lifecycle belongs.
#pragma once

#include "model/Chain.h"
#include "persist/ProjectPersist.h"

// Reads the chain into the persisted shape.
PersistChain captureChain(const ChainState &chain);

// Index of the installed plugin a persisted node names, or -1 when that plugin
// is not installed any more. The identifier wins; the label is the fallback for
// a project written before the identifier was recorded.
int findPluginIndex(const std::string &identifier, const std::string &labelFallback);

// Applies the persisted parameter values of one node to an instance that already
// exists, including the migrations of parameters that were renamed.
void restoreNodeParams(Node &node, const PersistNode &pn);
