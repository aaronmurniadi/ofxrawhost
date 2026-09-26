// Decode (RAW via Core Image), convert to/from OFX float buffers, export.
#pragma once

#import <CoreImage/CoreImage.h>

#include <memory>
#include <vector>

using Pixels = std::shared_ptr<std::vector<float>>;

CIImage *loadImage(NSURL *url);
// Renders to scene-linear Rec.2020, bottom-up rows (OFX order). maxEdge 0 = full size.
Pixels renderSource(CIContext *ctx, CIImage *img, int maxEdge, int &w, int &h);
// Wraps bottom-up float RGBA as a top-down CGImage tagged with `space`.
CGImageRef makeCGImage(const std::vector<float> &px, int w, int h, CFStringRef space);
// Format from the URL extension; EXR stays float, everything else is 16-bit.
bool writeImage(CGImageRef img, NSURL *url);
