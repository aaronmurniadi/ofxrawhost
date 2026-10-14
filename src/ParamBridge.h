// Bridge between the host UI/state and OFX plugin parameters: the change
// notification, the cached UI metadata refresh, and the color-space parameters
// the host drives.
#pragma once

#include "AppState.h"
#include "ofx/OfxTypes.h"

#include <vector>

void syncChainColorSpaces(App &app);
// Host color space that matches a plug-in color space option name. Returns false
// when the name has no close host tag.
bool hostSpaceForPluginName(const std::string &name, ColorSpace &out);
// True when at least one node is enabled, so a chain output exists at all.
bool chainHasEnabledNode(App &app);
void notifyChanged(Node &node, Param *p);
const std::vector<Val> &choiceOptions(Param *p);
