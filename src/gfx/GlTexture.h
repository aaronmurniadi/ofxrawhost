#pragma once

// Owns one GL_TEXTURE_2D and its pixel-buffer objects. The GL entry points and
// the upload state machine live in GlTexture.cpp, so this header stays free of
// OpenGL. Copy is not allowed, because two owners would delete the same texture.
struct GlTexture {
  unsigned int id = 0;
  int w = 0, h = 0;

  GlTexture() = default;
  GlTexture(const GlTexture &) = delete;
  GlTexture &operator=(const GlTexture &) = delete;
  GlTexture(GlTexture &&o) noexcept;
  GlTexture &operator=(GlTexture &&o) noexcept;
  ~GlTexture();

  // Uploads top-down RGBA8. Falls back to the synchronous path when the
  // streaming entry points are unavailable; never leaves the texture blank.
  void upload(const unsigned char *rgba, int width, int height);
  void destroy();

 private:
  unsigned int pbo[2] = {0, 0};
  int pboIndex = 0;
  bool storageAllocated = false;

  void moveFrom(GlTexture &o);
};
