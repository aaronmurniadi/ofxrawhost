#include "platform/SystemUiFont.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreText/CoreText.h>
#endif

namespace {

float saneOrDefault(float pt, float fallback) {
  if (!std::isfinite(pt) || pt < 1.0f) return fallback;
  return pt;
}

#if defined(__linux__)
// Reads gtk-font-name from the GTK settings file, for example "Sans 10". The
// trailing number is the size in points. Returns 0 when the file or the key is
// absent.
float gtkFontSizePt() {
  const char *home = std::getenv("HOME");
  if (!home || !home[0]) return 0.0f;
  const char *cands[] = {
      "/.config/gtk-4.0/settings.ini",
      "/.config/gtk-3.0/settings.ini",
  };
  for (const char *rel : cands) {
    std::ifstream in(std::string(home) + rel);
    if (!in) continue;
    std::string line;
    while (std::getline(in, line)) {
      const size_t key = line.find("gtk-font-name");
      if (key == std::string::npos) continue;
      const char *cursor = line.c_str() + key;
      float value = 0.0f;
      // The last number on the line is the point size; the family name comes first.
      for (const char *p = cursor; *p; ++p) {
        if (*p < '0' || *p > '9') continue;
        char *end = nullptr;
        const float parsed = std::strtof(p, &end);
        if (end != p && parsed > 0.0f) value = parsed;
        if (end == p) break;
        p = end - 1;
      }
      if (value > 0.0f) return value;
    }
  }
  return 0.0f;
}
#endif

}  // namespace

float systemUiFontSizePt() {
#if defined(_WIN32)
  // The message font is the font Windows uses for most interface text. lfHeight
  // is a character height in pixels at the system DPI, and is negative for a
  // character height, so divide by the system DPI to get points.
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    return 9.0f;
  int height = metrics.lfMessageFont.lfHeight;
  if (height == 0) return 9.0f;
  if (height < 0) height = -height;
  int dpi = 96;
  if (HDC dc = GetDC(nullptr)) {
    const int measured = GetDeviceCaps(dc, LOGPIXELSY);
    if (measured > 0) dpi = measured;
    ReleaseDC(nullptr, dc);
  }
  return saneOrDefault((float)height * 72.0f / (float)dpi, 9.0f);
#elif defined(__APPLE__)
  // The size-0 request returns the system font at its native size, 13 pt.
  CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 0.0, nullptr);
  if (!font) return 13.0f;
  const CGFloat size = CTFontGetSize(font);
  CFRelease(font);
  return saneOrDefault((float)size, 13.0f);
#elif defined(__linux__)
  const float gtk = gtkFontSizePt();
  if (gtk > 0.0f) return saneOrDefault(gtk, 11.0f);
  return 11.0f;
#else
  return 11.0f;
#endif
}
