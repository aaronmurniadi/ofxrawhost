// Minimal still-image OpenFX host: decode RAW/raster, run one OFX filter, preview, export.

#include "ImageIO.h"
#include "OfxHost.h"
#include "UI.h"

#include <tiffio.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int fail(const char *msg) {
  fprintf(stderr, "selftest FAILED: %s\n", msg);
  return 1;
}

static bool writeTinyTiff(const fs::path &p, bool halfFloat) {
  TIFF *tif = TIFFOpen(p.c_str(), "w");
  if (!tif) return false;
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, 2);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, 2);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, halfFloat ? SAMPLEFORMAT_IEEEFP : SAMPLEFORMAT_UINT);
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

// Renders a gray ramp through every installed filter plugin and writes export formats.
static int selfTest() {
  for (bool half : {false, true}) {
    const fs::path p = fs::temp_directory_path() / (half ? "ofxrawhost-selftest-half.tif" : "ofxrawhost-selftest.tif");
    if (!writeTinyTiff(p, half)) return fail(half ? "tiff write half" : "tiff write");
    Image img;
    ColorSpace cs = ColorSpace::sRGB;
    if (!loadImage(p.string(), img, cs) || img.w != 2 || img.h != 2) return fail(half ? "tiff load half" : "tiff load");
    if (img.px[(size_t)1 * 2 * 4 + 0] < 0.9f) return fail(half ? "tiff pixels half" : "tiff pixels");
    // Untagged float TIFF → Rec.2020; untagged 16-bit int → sRGB.
    if (half && cs != ColorSpace::LinearRec2020) return fail("tiff half colorspace");
    if (!half && cs != ColorSpace::sRGB) return fail("tiff uint colorspace");
    fs::remove(p);
  }

  {
    // Minimal 1x1 RGB PNG, no iCCP (untagged LDR → sRGB).
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
        0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xc9,
        0xfe, 0x92, 0xef, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    const fs::path p = fs::temp_directory_path() / "ofxrawhost-selftest-cs.png";
    FILE *f = fopen(p.c_str(), "wb");
    if (!f || fwrite(kPng, 1, sizeof kPng, f) != sizeof kPng) {
      if (f) fclose(f);
      return fail("png write");
    }
    fclose(f);
    Image img;
    ColorSpace cs = ColorSpace::LinearRec2020;
    if (!loadImage(p.string(), img, cs) || cs != ColorSpace::sRGB) return fail("png colorspace");
    fs::remove(p);
  }

  Image src;
  src.w = 64;
  src.h = 48;
  src.px.assign((size_t)src.w * src.h * 4, 1.0f);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      for (int c = 0; c < 3; ++c) src.px[((size_t)y * src.w + x) * 4 + c] = 0.18f * std::exp2((x - src.w / 2) / 8.0f);

  // Row-order check: bottom-up means index 0 is the bottom row.
  Image order;
  order.w = 8;
  order.h = 8;
  order.px.assign(8 * 8 * 4, 0.0f);
  for (int x = 0; x < 8; ++x) {
    order.px[((size_t)7 * 8 + x) * 4 + 0] = 1.0f;  // top row in display = last bottom-up row
    order.px[((size_t)7 * 8 + x) * 4 + 3] = 1.0f;
  }
  if (order.px[0] > 0.1f || order.px[(size_t)7 * 8 * 4] < 0.5f) return fail("source rows are not bottom-up");

  loadPlugins();
  if (gPlugins.empty()) return fail("no OFX filter plugins found");
  for (auto &pe : gPlugins) {
    auto e = createInstance(pe);
    if (!e) return fail(("createInstance: " + pe.label).c_str());
    Image out;
    out.w = src.w;
    out.h = src.h;
    out.px.assign(src.px.size(), -1.0f);
    const OfxStatus st = renderEffect(pe.plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, 0);
    callAction(pe.plugin, kOfxActionDestroyInstance, e.get());
    if (st != kOfxStatOK) return fail(("render: " + pe.label).c_str());
    bool finite = true, touched = false;
    for (float v : out.px) {
      finite &= std::isfinite(v);
      touched |= v != -1.0f;
    }
    if (!finite || !touched) return fail(("output: " + pe.label).c_str());
    const fs::path dir = fs::temp_directory_path();
    bool written = true;
    for (const char *ext : {"png", "jpg"}) {
      const fs::path p = dir / ("ofxrawhost-selftest." + std::string(ext));
      written &= writeImage(out, p.string());
      fs::remove(p);
    }
    if (!written) return fail("export");
    printf("ok  %s\n", pe.label.c_str());
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return selfTest();
  const std::string path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";
  return runApp(path);
}
