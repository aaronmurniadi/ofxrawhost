// Bridge between the host UI/state and OFX plugin parameters: the change
// notification, the cached UI metadata refresh, and the color-space parameters
// the host drives.
#pragma once

#include "AppState.h"
#include "ofx/OfxTypes.h"

#include <vector>

void syncOutputTag(App &app);
void notifyChanged(Node &node, Param *p);
void applyColorDefaults(App &app, Node &node);
const std::vector<Val> &choiceOptions(Param *p);
