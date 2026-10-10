#include "ofx/OfxMetal.h"

// Non-Apple build. There is no Metal on Linux or Windows, so every entry point
// is a no-op and the host runs the CPU render path. The Metal render path in the
// rest of the code stays compiled and inert, which keeps the call sites free of
// platform conditionals.

struct OfxMetalBuffer {};

bool ofxMetalInit() { return false; }

bool ofxMetalAvailable() { return false; }

OfxMetalBuffer *ofxMetalBufferCreate(size_t) { return nullptr; }

void ofxMetalBufferRelease(OfxMetalBuffer *) {}

void *ofxMetalBufferContents(OfxMetalBuffer *) { return nullptr; }

void *ofxMetalBufferHandle(OfxMetalBuffer *) { return nullptr; }

void *ofxMetalCommandQueue() { return nullptr; }

void ofxMetalSync() {}
