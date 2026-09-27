#pragma once

#include <cstddef>

// Minimal Metal plumbing for OFX GPU render. C ABI so it can be called from
// plain C++ translation units. All functions are thread-safe.

// Create the Metal device and command queue. Returns true on success.
// Safe to call multiple times; only the first call does work.
bool ofxMetalInit();

// True when Metal is initialized and not disabled via OFX_HOST_METAL=0.
bool ofxMetalAvailable();

// Opaque handle wrapping an MTLBuffer.
typedef struct OfxMetalBuffer OfxMetalBuffer;

// Create a buffer of `bytes` length using shared storage (CPU-accessible).
// Returns nullptr on failure.
OfxMetalBuffer *ofxMetalBufferCreate(size_t bytes);

// Release a buffer and free its storage.
void ofxMetalBufferRelease(OfxMetalBuffer *buf);

// CPU pointer to the buffer contents. Valid for shared storage; the pointer
// stays valid until the buffer is released.
void *ofxMetalBufferContents(OfxMetalBuffer *buf);

// The underlying id<MTLBuffer> as an opaque void* (for kOfxImagePropData).
void *ofxMetalBufferHandle(OfxMetalBuffer *buf);

// The host command queue as an opaque void* (for kOfxImageEffectPropMetalCommandQueue).
void *ofxMetalCommandQueue();

// Block until all GPU work enqueued on the host queue so far has completed.
// Must be called before CPU reads of buffer contents written by the GPU.
void ofxMetalSync();
