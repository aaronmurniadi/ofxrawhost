// Factories of the effects that ship inside OfxRawHost.ofx. The OpenFX support
// library calls OFX::Plugin::getPluginIDs once per binary, so one translation
// unit owns that entry point and each effect registers through these hooks.
#pragma once

#include "ofxsImageEffect.h"

void registerTransformPlugin(OFX::PluginFactoryArray &ids);
void registerAutoExposurePlugin(OFX::PluginFactoryArray &ids);
