// Internal to the OFX adapter: the property-suite entry points and the suite
// table accessors. Each suite lives in its own file now, so the tables that point
// at them are exposed to OfxSuites.cpp, which answers fetchSuite.

#pragma once

#include "ofxCore.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxProperty.h"

// One suite per file, so a reader looking for a clip callback does not have to
// scroll past the multithread pool.
const OfxPropertySuiteV1 &propSuiteTable();
const OfxMessageSuiteV1 &messageSuiteTable();
const OfxMemorySuiteV1 &memorySuiteTable();
const OfxMultiThreadSuiteV1 &threadSuiteTable();
