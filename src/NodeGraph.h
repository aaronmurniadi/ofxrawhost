#pragma once

#include "AppState.h"
#include "persist/ProjectPersist.h"

Node *selectedNode(App &app);
void clearNodes(App &app);
bool addNode(App &app, int pluginIndex);
void destroyNode(App &app, int index);
void moveNode(App &app, int from, int to);

void syncOutputTag(App &app);
void notifyChanged(Node &node, Param *p);
void applyColorDefaults(App &app, Node &node);
const std::vector<Val> &choiceOptions(Param *p);

PersistChain captureChain(const App &app);
void applyChain(App &app, const PersistChain &chain);
