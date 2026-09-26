#import "ImageIO.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cmath>

CIImage *loadImage(NSURL *url) {
  if (CIRAWFilter *raw = [CIRAWFilter filterWithImageURL:url]) {
    raw.boostAmount = 0;  // linear, no film-like tone curve
    if (raw.localToneMapSupported) raw.localToneMapAmount = 0;
    return raw.outputImage;
  }
  return [CIImage imageWithContentsOfURL:url options:@{kCIImageApplyOrientationProperty : @YES}];
}

static void flipRows(float *px, int w, int h) {
  const size_t row = (size_t)w * 4;
  std::vector<float> tmp(row);
  for (int y = 0; y < h / 2; ++y) {
    float *a = px + y * row, *b = px + (size_t)(h - 1 - y) * row;
    std::copy(a, a + row, tmp.data());
    std::copy(b, b + row, a);
    std::copy(tmp.begin(), tmp.end(), b);
  }
}

Pixels renderSource(CIContext *ctx, CIImage *img, int maxEdge, int &w, int &h) {
  CGRect ext = img.extent;
  const double longEdge = std::max(ext.size.width, ext.size.height);
  if (maxEdge > 0 && longEdge > maxEdge) {
    img = [img imageByApplyingFilter:@"CILanczosScaleTransform"
                 withInputParameters:@{kCIInputScaleKey : @(maxEdge / longEdge), kCIInputAspectRatioKey : @1}];
    ext = img.extent;
  }
  img = [img imageByApplyingTransform:CGAffineTransformMakeTranslation(-ext.origin.x, -ext.origin.y)];
  w = (int)std::floor(ext.size.width);
  h = (int)std::floor(ext.size.height);
  auto px = std::make_shared<std::vector<float>>((size_t)w * h * 4);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearITUR_2020);
  [ctx render:img toBitmap:px->data() rowBytes:w * 16 bounds:CGRectMake(0, 0, w, h) format:kCIFormatRGBAf colorSpace:cs];
  CGColorSpaceRelease(cs);
  flipRows(px->data(), w, h);  // Core Image writes top row first
  return px;
}

CGImageRef makeCGImage(const std::vector<float> &px, int w, int h, CFStringRef space) {
  NSMutableData *data = [NSMutableData dataWithBytes:px.data() length:px.size() * sizeof(float)];
  flipRows(static_cast<float *>(data.mutableBytes), w, h);
  CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
  CGColorSpaceRef cs = CGColorSpaceCreateWithName(space);
  CGImageRef img = CGImageCreate(w, h, 32, 128, w * 16, cs,
                                 kCGImageAlphaNoneSkipLast | kCGBitmapFloatComponents | kCGBitmapByteOrder32Host,
                                 provider, nullptr, false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(cs);
  CGDataProviderRelease(provider);
  return img;
}

static CGImageRef to16Bit(CGImageRef img) {
  CGContextRef ctx = CGBitmapContextCreate(nullptr, CGImageGetWidth(img), CGImageGetHeight(img), 16, 0,
                                           CGImageGetColorSpace(img), kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder16Host);
  if (!ctx) return CGImageRetain(img);
  CGContextDrawImage(ctx, CGRectMake(0, 0, CGImageGetWidth(img), CGImageGetHeight(img)), img);
  CGImageRef out = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  return out;
}

bool writeImage(CGImageRef img, NSURL *url) {
  UTType *type = [UTType typeWithFilenameExtension:url.pathExtension] ?: UTTypeTIFF;
  const bool keepFloat = [type conformsToType:[UTType typeWithIdentifier:@"com.ilm.openexr-image"]];
  CGImageRef out = keepFloat ? CGImageRetain(img) : to16Bit(img);
  CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)type.identifier, 1, nullptr);
  bool ok = false;
  if (dest) {
    CGImageDestinationAddImage(dest, out, (__bridge CFDictionaryRef) @{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality : @0.92});
    ok = CGImageDestinationFinalize(dest);
    CFRelease(dest);
  }
  CGImageRelease(out);
  return ok;
}

