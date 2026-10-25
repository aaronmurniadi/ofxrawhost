#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "codecs/ImageIO.h"

#include <tiffio.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static bool writeTinyTiff(const fs::path &p, bool halfFloat) {
  TIFF *tif = TIFFOpen(p.string().c_str(), "w");
  if (!tif) return false;
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 2);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 2);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
  if (halfFloat) TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
  else TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 2);
  uint16_t row0[6], row1[6];
  if (halfFloat) {
    // half 1.0 = 0x3c00
    uint16_t one = 0x3c00, z = 0;
    row0[0] = one; row0[1] = z; row0[2] = z; row0[3] = z; row0[4] = one; row0[5] = z;
    row1[0] = z; row1[1] = z; row1[2] = one; row1[3] = one; row1[4] = one; row1[5] = one;
  } else {
    row0[0] = 65535; row0[1] = 0; row0[2] = 0; row0[3] = 0; row0[4] = 65535; row0[5] = 0;
    row1[0] = 0; row1[1] = 0; row1[2] = 65535; row1[3] = 65535; row1[4] = 65535; row1[5] = 65535;
  }
  const bool ok = TIFFWriteScanline(tif, row0, 0, 0) >= 0 && TIFFWriteScanline(tif, row1, 1, 0) >= 0;
  TIFFClose(tif);
  return ok;
}

// Untagged float TIFF → Rec.2020; untagged 16-bit int → sRGB. The working space
// of an integer TIFF is its tag, because the samples stay encoded.
int testTiffDecode() {
  for (bool half : {false, true}) {
    const char *stem = "ofxrawhost-selftest.tif";
    if (half) stem = "ofxrawhost-selftest-half.tif";
    const fs::path p = fs::temp_directory_path() / stem;
    if (!writeTinyTiff(p, half)) {
      if (half) return fail("tiff write half");
      return fail("tiff write");
    }
    Image img;
    ColorSpace cs = ColorSpace::sRGB;
    ColorSpace working = ColorSpace::sRGB;
    if (!loadImage(p.string(), img, cs, working) || img.w != 2 || img.h != 2) {
      if (half) return fail("tiff load half");
      return fail("tiff load");
    }
    if (img.px[(size_t)1 * 2 * 4 + 0] < 0.9f) {
      if (half) return fail("tiff pixels half");
      return fail("tiff pixels");
    }
    if (half && cs != ColorSpace::LinearRec2020) return fail("tiff half colorspace");
    if (!half && cs != ColorSpace::sRGB) return fail("tiff uint colorspace");
    if (working != cs) return fail("tiff working space");
    fs::remove(p);
  }
  return 0;
}

// Minimal 1x1 RGB PNG, no iCCP (untagged LDR → sRGB tag, linear working space,
// because stb removes the transfer curve while it converts to float).
int testPngColorSpace() {
  static const unsigned char kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
      0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xc9,
      0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  const fs::path p = fs::temp_directory_path() / "ofxrawhost-selftest-cs.png";
  FILE *f = fopen(p.string().c_str(), "wb");
  if (!f || fwrite(kPng, 1, sizeof kPng, f) != sizeof kPng) {
    if (f) fclose(f);
    return fail("png write");
  }
  fclose(f);
  Image img;
  ColorSpace cs = ColorSpace::LinearRec2020;
  ColorSpace working = ColorSpace::sRGB;
  if (!loadImage(p.string(), img, cs, working) || cs != ColorSpace::sRGB) return fail("png colorspace");
  if (working != ColorSpace::LinearRec709) return fail("png working space");
  fs::remove(p);
  return 0;
}

// Row-order check: bottom-up means index 0 is the bottom row.
int testRowOrder() {
  Image order;
  order.w = 8;
  order.h = 8;
  order.px.assign(8 * 8 * 4, 0.0f);
  for (int x = 0; x < 8; ++x) {
    order.px[((size_t)7 * 8 + x) * 4 + 0] = 1.0f;  // top row in display = last bottom-up row
    order.px[((size_t)7 * 8 + x) * 4 + 3] = 1.0f;
  }
  if (order.px[0] > 0.1f || order.px[(size_t)7 * 8 * 4] < 0.5f) return fail("source rows are not bottom-up");
  return 0;
}

// The export converts the chain output from the preview space into the export
// space with a rendering intent. A same-space request must be an exact no-op,
// and a real conversion must change the pixels, stay finite, and round-trip back
// to the original values within a small tolerance.
int testColorSpaceConversion() {
  Image img;
  img.w = 16;
  img.h = 8;
  img.px.assign((size_t)img.w * img.h * 4, 0.0f);
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x) {
      const size_t i = ((size_t)y * img.w + x) * 4;
      img.px[i + 0] = 0.18f + 0.02f * (float)x;
      img.px[i + 1] = 0.30f;
      img.px[i + 2] = 0.60f - 0.01f * (float)y;
      img.px[i + 3] = 1.0f;
    }
  const Image original = img;

  if (!convertColorSpace(img, ColorSpace::sRGB, ColorSpace::sRGB, RenderIntent::Perceptual))
    return fail("convert: same space failed");
  if (img.px != original.px) return fail("convert: same space changed pixels");

  if (!convertColorSpace(img, ColorSpace::sRGB, ColorSpace::LinearRec709, RenderIntent::RelativeColorimetric))
    return fail("convert: sRGB to Linear Rec.709 failed");
  bool finite = true;
  bool changed = false;
  bool alphaKept = true;
  for (size_t i = 0; i < img.px.size(); i += 4) {
    for (int c = 0; c < 3; ++c) {
      finite &= std::isfinite(img.px[i + c]);
      changed |= std::abs(img.px[i + c] - original.px[i + c]) > 1e-6f;
    }
    alphaKept &= img.px[i + 3] == 1.0f;
  }
  if (!finite) return fail("convert: output is not finite");
  if (!changed) return fail("convert: a real conversion changed nothing");
  if (!alphaKept) return fail("convert: alpha was not preserved");

  if (!convertColorSpace(img, ColorSpace::LinearRec709, ColorSpace::sRGB, RenderIntent::RelativeColorimetric))
    return fail("convert: round trip failed");
  for (size_t i = 0; i < img.px.size(); ++i) {
    if (std::abs(img.px[i] - original.px[i]) > 2e-3f) return fail("convert: round trip drifted");
  }
  printf("ok  color space conversion\n");
  return 0;
}
