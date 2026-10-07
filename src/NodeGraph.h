// Chain model operations: the node list, selection, and effect lifecycle.
// Persistence lives in persist/ChainIO.h; OFX parameter wiring lives in
// ParamBridge.h.
#pragma once

#include "AppState.h"
#include "ofx/OfxTypes.h"

Node *selectedNode(App &app);
void clearNodes(App &app);
bool addNode(App &app, int pluginIndex);
void destroyNode(App &app, int index);
void moveNode(App &app, int from, int to);
void setNodeEnabled(App &app, int index, bool enabled);
