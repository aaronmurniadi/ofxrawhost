// Decode RAW (LibRaw) / raster (stb), convert to/from OFX float buffers, export.
#pragma once

#include <string>
#include <vector>

struct Image {
  std::vector<float> px;  // bottom-up float RGBA
  int w = 0, h = 0;
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

// Loads RAW via LibRaw (linear, camera WB) or PNG/JPEG/TIFF/EXR via stb/tinyexr.
bool loadImage(const std::string &path, Image &out);
// maxEdge 0 = full size; otherwise downsamples so longest edge <= maxEdge.
bool makePreview(const Image &src, int maxEdge, Image &out);
// Format from path extension; EXR stays float (chromaticities), PNG/JPEG/TIFF embed ICC.
bool writeImage(const Image &img, const std::string &path, ColorSpace space = ColorSpace::sRGB, int jpegQuality = 92);
// Top-down 8-bit RGBA for display (lcms2 transform into sRGB).
void toDisplayRGBA8(const Image &img, ColorSpace space, std::vector<unsigned char> &out);
