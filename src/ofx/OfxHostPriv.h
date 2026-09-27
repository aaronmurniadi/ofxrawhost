#pragma once

#include "ofx/OfxHost.h"

#include <memory>

// Shared between OfxSuites.cpp and OfxPlugins.cpp
extern OfxHost gOfxHost;
bool ofxActionOk(OfxStatus s);
std::unique_ptr<Effect> cloneEffect(const Effect &src);
bool isStringType(const std::string &t);
