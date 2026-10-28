// Chain model operations: the node list, selection, and effect lifecycle.
// Persistence lives in ChainIO.h; OFX parameter wiring lives in ParamBridge.h.
// Restoring a whole chain from persisted data lives here too, because it creates
// and destroys instances and must keep the render worker idle while it does.
#pragma once

#include "services/ManagerSession.h"
#include "ofx/OfxTypes.h"

struct PersistChain;

Node *selectedNode(ManagerSession session);
void clearNodes(ManagerSession session);
bool addNode(ManagerSession session, int pluginIndex);
// Replaces the chain with the persisted one: the instances are recreated, the
// parameter values restored, and the color spaces and render rescheduled once,
// after the whole chain is in place.
void applyChain(ManagerSession session, const PersistChain &chain);
void destroyNode(ManagerSession session, int index);
void moveNode(ManagerSession session, int from, int to);
void setNodeEnabled(ManagerSession session, int index, bool enabled);
