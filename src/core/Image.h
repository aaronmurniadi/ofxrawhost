// Shared value types: the pixel buffer, the color space tags, the rendering
// intents, the camera metadata, and the encode settings.
//
// Everything here is plain data with no dependency on a decoder, an export
// format, or the UI. The codecs in io/, the model, the persist layer and the
// OFX adapter all need these, so they live below all of them.

#pragma once

#include <string>
#include <utility>
#include <vector>

struct Image {
  std::vector<float> px;  // bottom-up float RGBA
  int w = 0, h = 0;
  void swap(Image &o) noexcept {
    px.swap(o.px);
    std::swap(w, o.w);
    std::swap(h, o.h);
  }
};

// Matches the UI color space combos. Plugin pixels are assumed already in this space;
// we only embed the matching ICC (and convert for on-screen preview).
enum class ColorSpace {
  sRGB = 0,
  DisplayP3,
  LinearRec709,
  LinearRec2020,
};

// The single source of the color space names: the combos list this array and a
// project or a sidecar stores the same strings, so the two cannot drift apart.
// The index is the enum value, so the array size is a compile-time check.
inline constexpr int kColorSpaceCount = 4;
inline constexpr const char *kColorSpaces[kColorSpaceCount] = {"sRGB", "Display P3", "Linear Rec.709",
                                                             "Linear Rec.2020"};

inline const char *colorSpaceName(ColorSpace cs) {
  const int index = static_cast<int>(cs);
  if (index < 0 || index >= kColorSpaceCount) return kColorSpaces[0];
  return kColorSpaces[index];
}

// Host color space whose name matches name, or fallback when there is none. The
// inverse of colorSpaceName, for the names stored in a project or a sidecar.
ColorSpace colorSpaceFromName(const std::string &name, ColorSpace fallback);

// Working buffer space used for a file whose samples are linear. A gamma tag
// (sRGB, Display P3) describes the file, so the linear counterpart is the space
// of the values after the transfer curve is removed. The host has no linear-P3
// tag, so Display P3 uses the closest wider linear space.
inline ColorSpace linearWorkingSpace(ColorSpace fileOrTag) {
  switch (fileOrTag) {
    case ColorSpace::sRGB:
      return ColorSpace::LinearRec709;
    case ColorSpace::DisplayP3:
      return ColorSpace::LinearRec2020;
    case ColorSpace::LinearRec709:
    case ColorSpace::LinearRec2020:
      return fileOrTag;
  }
  return ColorSpace::LinearRec709;
}

// Rendering intent of a color-space conversion. The order matches the lcms2
// INTENT_ constants, so the value passes straight through as the intent.
enum class RenderIntent { Perceptual = 0, RelativeColorimetric, Saturation, AbsoluteColorimetric };
inline constexpr int kRenderIntentCount = 4;
inline constexpr const char *kRenderIntents[kRenderIntentCount] = {
    "Perceptual", "Relative colorimetric", "Saturation", "Absolute colorimetric"};

inline const char *renderIntentName(RenderIntent intent) {
  const int index = static_cast<int>(intent);
  if (index < 0 || index >= kRenderIntentCount) return kRenderIntents[0];
  return kRenderIntents[index];
}

// Camera metadata of a decoded file, filled by the RAW decoders only. A file that
// carries none (a JPEG, a TIFF, or a RAW whose maker LibRaw does not report)
// leaves `hasCamera` false.
struct ImageMeta {
  bool hasCamera = false;
  std::string cameraMaker;
  std::string cameraModel;
  int iso = 0;
};

// Encoding settings for the raster export formats. Formats ignore what they cannot
// use: PNG and TIFF honor bitDepth, JPEG ignores it (always 8-bit), and WebP is
// always 8-bit. quality applies to JPEG, WebP, and JPEG XL; lossless applies to
// WebP and JPEG XL only.
struct EncodeOptions {
  int bitDepth = 8;      // 8 or 16
  int quality = 92;      // 1..100 (lossy formats)
  bool lossless = false; // WebP / JPEG XL
};
