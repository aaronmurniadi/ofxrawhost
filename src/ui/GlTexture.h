#pragma once

// GL symbols come from GLFW, as in the other render files. Do not include gl.h here.
#include <GLFW/glfw3.h>

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
  void moveFrom(GlTexture &o) {
    id = o.id;
    w = o.w;
    h = o.h;
    o.id = 0;
    o.w = o.h = 0;
  }
};

inline void GlTexture::upload(const unsigned char *rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0) return;
  if (!id) glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (w != width || h != height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    w = width;
    h = height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
}

inline void GlTexture::destroy() {
  if (id) glDeleteTextures(1, &id);
  id = 0;
  w = h = 0;
}
