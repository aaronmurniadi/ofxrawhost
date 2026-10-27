// Bridge between the host UI/state and OFX plugin parameters: the change
// notification, the cached UI metadata refresh, and the color-space parameters
// the host drives.
#pragma once

#include "services/Workspace.h"
#include "ofx/OfxTypes.h"

#include <vector>

void syncChainColorSpaces(Workspace &app);
// Host color space that matches a plug-in color space option name. Returns false
// when the name has no close host tag.
bool hostSpaceForPluginName(const std::string &name, ColorSpace &out);
// Fills the noise model of one node from the profile that matches the camera and
// ISO of the open frame. Returns the number of parameters it moved, so the caller
// can tell whether a profile was applied.
int seedNoiseModelParams(Workspace &app, Node &node);

// Seeds every node in the chain, for an image the workspace has just switched to.
// Returns the number of nodes that took a profile.
int seedNoiseModelParams(Workspace &app);

// True when at least one node is enabled, so a chain output exists at all.
bool chainHasEnabledNode(Workspace &app);
void notifyChanged(Node &node, Param *p);
const std::vector<Val> &choiceOptions(Param *p);
