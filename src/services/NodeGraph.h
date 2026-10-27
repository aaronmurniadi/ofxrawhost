// Chain model operations: the node list, selection, and effect lifecycle.
// Persistence lives in ChainIO.h; OFX parameter wiring lives in ParamBridge.h.
// Restoring a whole chain from persisted data lives here too, because it creates
// and destroys instances and must keep the render worker idle while it does.
#pragma once

#include "services/Workspace.h"
#include "ofx/OfxTypes.h"

struct PersistChain;

Node *selectedNode(Workspace &app);
void clearNodes(Workspace &app);
bool addNode(Workspace &app, int pluginIndex);
// Replaces the chain with the persisted one: the instances are recreated, the
// parameter values restored, and the color spaces and render rescheduled once,
// after the whole chain is in place.
void applyChain(Workspace &app, const PersistChain &chain);
void destroyNode(Workspace &app, int index);
void moveNode(Workspace &app, int from, int to);
void setNodeEnabled(Workspace &app, int index, bool enabled);
