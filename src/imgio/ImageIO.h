// Decode RAW (LibRaw) / raster (stb), convert to/from OFX float buffers, export.
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

const char *colorSpaceName(ColorSpace cs);
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
const char *renderIntentName(RenderIntent intent);

// Loads RAW via LibRaw (linear, camera WB), TIFF via libtiff, or PNG/JPEG/EXR via stb/tinyexr.
// detected: inferred Input Color Space (RAW / untagged float → Linear Rec.2020; untagged LDR → sRGB;
// embedded ICC → nearest of the four tags). Does not convert pixels.
// working: color space of the returned buffer. It equals detected when the
// samples are stored as they are (integer TIFF, EXR), and the linear counterpart
// of detected when the decoder removed the transfer curve (RAW, PNG/JPEG).
// The preview display and the plugin Input Color Space read working, while
// detected describes the file and travels in the sidecar and the export metadata.
bool loadImage(const std::string &path, Image &out, ColorSpace &detected, ColorSpace &working);
// Lowercase file extension with the leading dot (for example ".cr2"). Empty when there is none.
std::string lowerFileExtension(const std::string &path);
// One table in ImageLoad.cpp lists the supported extensions. extLower includes the
// leading dot and is lowercase.
bool isRawImageExtension(const std::string &extLower);
bool isSupportedImageExtension(const std::string &extLower);
// Every supported extension, lowercase, with the leading dot, in table order.
std::vector<std::string> supportedImageExtensions();
// maxEdge 0 = full size; otherwise downsamples so longest edge <= maxEdge.
bool makePreview(const Image &src, int maxEdge, Image &out);
// Encoding settings for the raster export formats. Formats ignore what they cannot
// use: PNG and TIFF honor bitDepth, JPEG ignores it (always 8-bit), and WebP is
// always 8-bit. quality applies to JPEG, WebP, and JPEG XL; lossless applies to
// WebP and JPEG XL only.
struct EncodeOptions {
  int bitDepth = 8;      // 8 or 16
  int quality = 92;      // 1..100 (lossy formats)
  bool lossless = false; // WebP / JPEG XL
};

// Format from path extension; PNG/JPEG/TIFF/WebP/JPEG XL embed ICC.
bool writeImage(const Image &img, const std::string &path, ColorSpace space = ColorSpace::sRGB, EncodeOptions opts = {});
// Converts an image in place between two color spaces with the given rendering
// intent. Returns false when a profile or a transform cannot be built, and then
// leaves the pixels untouched.
bool convertColorSpace(Image &img, ColorSpace from, ColorSpace to, RenderIntent intent);
// Top-down 8-bit RGBA of img, converted from `from` into `to` first. The bytes
// are the encoding of `to`, so they match what a file tagged `to` holds: a
// linear space gives the dark mid-tones such a file shows in a viewer without
// color management. The preview uses this with the preview color space.
void toDisplayRGBA8(const Image &img, ColorSpace from, ColorSpace to, std::vector<unsigned char> &out);
// Small filmstrip preview (downscaled source, sRGB 8-bit RGBA).
bool loadThumbnailRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w, int &h);
