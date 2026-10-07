// Private to the OFX module: the GPU scratch state behind Effect::gpu. Not part
// of the public instance model, so no Metal type leaks into OfxTypes.h.
#pragma once

#include "ofx/OfxTypes.h"

#include <cstddef>

struct EffectGpu {
  void *srcMtl = nullptr, *dstMtl = nullptr;   // owned scratch id<MTLBuffer>s when metalEnabled
  size_t srcMtlBytes = 0, dstMtlBytes = 0;     // capacities of the two owned buffers
  void *curSrcMtl = nullptr, *curDstMtl = nullptr;  // buffers for the in-flight render (may be chain-owned)
  bool metalEnabled = false;                   // this render passes MTLBuffer images
  bool metalCapable = false;                   // plugin declared kOfxImageEffectPropMetalRenderSupported
};

// Returns the effect's GPU state, allocating it on first use.
EffectGpu &effectGpu(Effect &e);
