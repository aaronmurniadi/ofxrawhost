#include "ofx/OfxMetal.h"

#import <Metal/Metal.h>

#import <cstdlib>

static id<MTLDevice> gDevice = nil;
static id<MTLCommandQueue> gQueue = nil;

bool ofxMetalInit() {
  if (gQueue) return true;
  gDevice = MTLCreateSystemDefaultDevice();
  if (!gDevice) return false;
  gQueue = [gDevice newCommandQueue];
  return gQueue != nil;
}

bool ofxMetalAvailable() {
  if (!ofxMetalInit()) return false;
  if (const char *env = std::getenv("OFX_HOST_METAL")) {
    if (env[0] == '0') return false;
  }
  return true;
}

struct OfxMetalBuffer {
  id<MTLBuffer> buffer;
};

OfxMetalBuffer *ofxMetalBufferCreate(size_t bytes) {
  if (!gQueue) return nil;
  // Shared storage: CPU and GPU see the same memory. On Apple Silicon this is
  // unified memory (zero-copy). On Intel Macs the GPU accesses system memory
  // over PCIe, which is slower than managed for pure GPU compute but correct and
  // still avoids the per-node CPU<->GPU copies the OFX Metal path eliminates.
  id<MTLBuffer> buf = [gDevice newBufferWithLength:bytes options:MTLResourceStorageModeShared];
  if (!buf) return nil;
  auto *wrapper = new OfxMetalBuffer;
  wrapper->buffer = buf;
  return wrapper;
}

void ofxMetalBufferRelease(OfxMetalBuffer *buf) {
  if (!buf) return;
  [buf->buffer release];
  delete buf;
}

void *ofxMetalBufferContents(OfxMetalBuffer *buf) {
  return buf ? [buf->buffer contents] : nullptr;
}

void *ofxMetalBufferHandle(OfxMetalBuffer *buf) {
  return buf ? (__bridge void *)buf->buffer : nullptr;
}

void *ofxMetalCommandQueue() {
  return gQueue ? (__bridge void *)gQueue : nullptr;
}

void ofxMetalSync() {
  if (!gQueue) return;
  id<MTLCommandBuffer> cb = [gQueue commandBuffer];
  [cb commit];
  [cb waitUntilCompleted];
}
