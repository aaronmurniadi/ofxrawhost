#pragma once

// GL symbols come from GLFW, as in the other render files. Do not include gl.h here.
#include <GLFW/glfw3.h>

// The GL 1.5 pixel-buffer-object and GL 4.2 glTexStorage2D entry points are not
// guaranteed to be declared by the GL header pulled in via GLFW, so resolve them
// through glfwGetProcAddress into local function-pointer typedefs. Everything
// degrades to the synchronous path when a pointer is unavailable.
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace gltex_detail {

using GenBuffersFn = void (*)(int, unsigned int *);
using BindBufferFn = void (*)(unsigned int, unsigned int);
using BufferDataFn = void (*)(unsigned int, long, const void *, unsigned int);
using BufferSubDataFn = void (*)(unsigned int, long, long, const void *);
using DeleteBuffersFn = void (*)(int, const unsigned int *);
using TexStorage2DFn = void (*)(unsigned int, int, unsigned int, int, int);

struct PboFns {
  GenBuffersFn genBuffers = nullptr;
  BindBufferFn bindBuffer = nullptr;
  BufferDataFn bufferData = nullptr;
  BufferSubDataFn bufferSubData = nullptr;
  DeleteBuffersFn deleteBuffers = nullptr;
  TexStorage2DFn texStorage2D = nullptr;

  bool ok() const {
    if (!genBuffers) return false;
    if (!bindBuffer) return false;
    if (!bufferData) return false;
    if (!bufferSubData) return false;
    if (!deleteBuffers) return false;
    if (!texStorage2D) return false;
    return true;
  }
};

// Resolved once, lazily, on the render thread that owns the GL context.
inline const PboFns &pboFns() {
  static PboFns fns;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    fns.genBuffers =
        reinterpret_cast<GenBuffersFn>(glfwGetProcAddress("glGenBuffers"));
    fns.bindBuffer =
        reinterpret_cast<BindBufferFn>(glfwGetProcAddress("glBindBuffer"));
    fns.bufferData =
        reinterpret_cast<BufferDataFn>(glfwGetProcAddress("glBufferData"));
    fns.bufferSubData =
        reinterpret_cast<BufferSubDataFn>(glfwGetProcAddress("glBufferSubData"));
    fns.deleteBuffers =
        reinterpret_cast<DeleteBuffersFn>(glfwGetProcAddress("glDeleteBuffers"));
    fns.texStorage2D =
        reinterpret_cast<TexStorage2DFn>(glfwGetProcAddress("glTexStorage2D"));
  }
  return fns;
}

} // namespace gltex_detail

// Owns one GL_TEXTURE_2D. Copy is not allowed, because two owners would delete
// the same texture.
struct GlTexture {
  unsigned int id = 0;
  int w = 0, h = 0;

  GlTexture() = default;
  GlTexture(const GlTexture &) = delete;
  GlTexture &operator=(const GlTexture &) = delete;
  GlTexture(GlTexture &&o) noexcept { moveFrom(o); }
  GlTexture &operator=(GlTexture &&o) noexcept {
    if (this != &o) {
      destroy();
      moveFrom(o);
    }
    return *this;
  }
  ~GlTexture() { destroy(); }

  void upload(const unsigned char *rgba, int width, int height);
  void destroy();

 private:
  unsigned int pbo[2] = {0, 0};
  int pboIndex = 0;
  bool storageAllocated = false;

  void moveFrom(GlTexture &o) {
    id = o.id;
    w = o.w;
    h = o.h;
    pbo[0] = o.pbo[0];
    pbo[1] = o.pbo[1];
    pboIndex = o.pboIndex;
    storageAllocated = o.storageAllocated;
    o.id = 0;
    o.w = o.h = 0;
    o.pbo[0] = o.pbo[1] = 0;
    o.pboIndex = 0;
    o.storageAllocated = false;
  }
};

inline void GlTexture::upload(const unsigned char *rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0) return;
  const gltex_detail::PboFns &fns = gltex_detail::pboFns();
  bool sizeChanged = false;
  if (w != width) sizeChanged = true;
  if (h != height) sizeChanged = true;

  // Immutable storage cannot be respecified, so a size change recreates the
  // texture (and its PBOs) and allocates glTexStorage2D at the new dimensions.
  // Any failure leaves storageAllocated false or the PBOs absent, which routes
  // through the synchronous fallback below.
  if (fns.ok() && sizeChanged) {
    if (id) glDeleteTextures(1, &id);
    id = 0;
    if (pbo[0]) fns.deleteBuffers(1, &pbo[0]);
    if (pbo[1]) fns.deleteBuffers(1, &pbo[1]);
    pbo[0] = pbo[1] = 0;
    pboIndex = 0;
    storageAllocated = false;

    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    while (glGetError() != GL_NO_ERROR) {
    }
    fns.texStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    if (glGetError() == GL_NO_ERROR) {
      storageAllocated = true;
      w = width;
      h = height;
      fns.genBuffers(2, pbo);
      if (!pbo[0] || !pbo[1]) {
        if (pbo[0]) fns.deleteBuffers(1, &pbo[0]);
        if (pbo[1]) fns.deleteBuffers(1, &pbo[1]);
        pbo[0] = pbo[1] = 0;
      }
    } else {
      storageAllocated = false;
    }
  }

  if (!id) glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Asynchronous path: stream the pixels into one PBO while the GPU may still be
  // reading the other, then upload the texture from the PBO.
  if (storageAllocated && pbo[0] && pbo[1]) {
    int next = pboIndex;
    long bytes = (long)width * (long)height * 4L;
    while (glGetError() != GL_NO_ERROR) {
    }
    fns.bindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo[next]);
    // Orphan the previous contents so the driver hands back fresh storage.
    fns.bufferData(GL_PIXEL_UNPACK_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
    fns.bufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, bytes, rgba);
    if (glGetError() == GL_NO_ERROR) {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                      GL_UNSIGNED_BYTE, nullptr);
      fns.bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      pboIndex = 1 - next;
      return;
    }
    fns.bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

  // Synchronous fallback: never leaves the texture blank.
  if (w != width || h != height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    w = width;
    h = height;
    storageAllocated = false;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba);
  }
}

inline void GlTexture::destroy() {
  const gltex_detail::PboFns &fns = gltex_detail::pboFns();
  if (fns.deleteBuffers) {
    if (pbo[0]) fns.deleteBuffers(1, &pbo[0]);
    if (pbo[1]) fns.deleteBuffers(1, &pbo[1]);
  }
  pbo[0] = pbo[1] = 0;
  pboIndex = 0;
  storageAllocated = false;
  if (id) glDeleteTextures(1, &id);
  id = 0;
  w = h = 0;
}
