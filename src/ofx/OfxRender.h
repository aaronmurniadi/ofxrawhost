// OpenFX host render entry points: action dispatch, region-of-definition query,
// and one CPU-or-GPU effect render.
#pragma once

#include "ofx/OfxTypes.h"

#include <memory>

OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in = nullptr, PropSet *out = nullptr);

// Output size the plugin declares for input size inW×inH (kOfxImageEffectActionGetRegionOfDefinition).
// Falls back to inW×inH when the plugin does not override its RoD.
void queryOutputSize(OfxPlugin *p, Effect *e, int inW, int inH, int *outW, int *outH);

// src: bottom-up float RGBA w*h pixels. dst receives outW*outH pixels (capacity >= outW*outH).
// gen 0 = never aborted.
// srcMtl/dstMtl are optional id<MTLBuffer> handles for chained GPU renders. When a
// handle is given, the image lives on the GPU and the matching CPU pointer may be
// null. When dstMtl is null and the node renders on Metal, renderEffect syncs and
// copies the result back to dst, so single-node callers keep the old behavior. The
// caller owns the sync when dstMtl is given, and must call ofxMetalSync() before it
// reads that buffer on the CPU.
// draft selects kOfxImageEffectPropRenderQualityDraft for reduced-quality interactive passes.
OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int outW, int outH, int gen,
                       void *srcMtl = nullptr, void *dstMtl = nullptr, bool draft = false);

// True when the node renders through Metal on this machine.
bool effectUsesMetal(const Effect *e);
