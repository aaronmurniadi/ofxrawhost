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

// Matches the UI "Output tag" combo. Plugin pixels are assumed already in this space;
// we only embed the matching ICC (and convert for on-screen preview).
enum class ColorSpace {
  sRGB = 0,
  DisplayP3,
  LinearRec709,
  LinearRec2020,
};

const char *colorSpaceName(ColorSpace cs);

// Loads RAW via LibRaw (linear, camera WB), TIFF via libtiff, or PNG/JPEG/EXR via stb/tinyexr.
// detected: inferred Input Color Space (RAW / untagged float → Linear Rec.2020; untagged LDR → sRGB;
// embedded ICC → nearest of the four tags). Does not convert pixels.
bool loadImage(const std::string &path, Image &out, ColorSpace &detected);
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
// Top-down 8-bit RGBA for display (lcms2 transform into sRGB).
void toDisplayRGBA8(const Image &img, ColorSpace space, std::vector<unsigned char> &out);
// Small filmstrip preview (downscaled source, sRGB 8-bit RGBA).
bool loadThumbnailRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w, int &h);
