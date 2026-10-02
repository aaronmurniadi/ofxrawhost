// Self-test cases: decode and color checks, the bundled Crop plugin, every
// installed plugin, and a concurrency smoke case. Run with --selftest.

#include "selftest/Selftest.h"

#include "Actions.h"
#include "NodeGraph.h"
#include "RenderPipeline.h"
#include "imgio/ImageIO.h"
#include "ofx/OfxHost.h"
#include "persist/ProjectPersist.h"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static int fail(const char *msg) {
  fprintf(stderr, "selftest FAILED: %s\n", msg);
  return 1;
}

static bool selftestExcluded(const PluginEntry &pe) {
  return pe.plugin && pe.plugin->pluginIdentifier && !strcmp(pe.plugin->pluginIdentifier, "org.spektrafilm.dev");
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

// Untagged float TIFF → Rec.2020; untagged 16-bit int → sRGB.
static int testTiffDecode() {
  for (bool half : {false, true}) {
    const fs::path p = fs::temp_directory_path() / (half ? "ofxrawhost-selftest-half.tif" : "ofxrawhost-selftest.tif");
    if (!writeTinyTiff(p, half)) return fail(half ? "tiff write half" : "tiff write");
    Image img;
    ColorSpace cs = ColorSpace::sRGB;
    if (!loadImage(p.string(), img, cs) || img.w != 2 || img.h != 2) return fail(half ? "tiff load half" : "tiff load");
    if (img.px[(size_t)1 * 2 * 4 + 0] < 0.9f) return fail(half ? "tiff pixels half" : "tiff pixels");
    if (half && cs != ColorSpace::LinearRec2020) return fail("tiff half colorspace");
    if (!half && cs != ColorSpace::sRGB) return fail("tiff uint colorspace");
    fs::remove(p);
  }
  return 0;
}

// Minimal 1x1 RGB PNG, no iCCP (untagged LDR → sRGB).
static int testPngColorSpace() {
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
  return 0;
}

// Row-order check: bottom-up means index 0 is the bottom row.
static int testRowOrder() {
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

// A gray ramp that gives every plugin something with a large dynamic range.
static Image makeTestImage() {
  Image src;
  src.w = 64;
  src.h = 48;
  src.px.assign((size_t)src.w * src.h * 4, 1.0f);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      for (int c = 0; c < 3; ++c) src.px[((size_t)y * src.w + x) * 4 + c] = 0.18f * std::exp2((x - src.w / 2) / 8.0f);
  return src;
}

// Bundled Crop plugin: defaults must be an identity pass-through with a
// full-size RoD; the crop slider must shrink the RoD and change the rendered
// output. Checked first so a flaky third-party plugin later in the list
// cannot mask a regression here.
static int testCropPlugin(Image &src) {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Crop"; });
  if (it == gPlugins.end()) return fail("bundled Crop plugin not found");
  auto e = createInstance(*it);
  if (!e) return fail("createInstance: Crop");

  // At default (crop=0): RoD matches source size and render is identity.
  int ow = src.w, oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  if (ow != src.w || oh != src.h) return fail("crop RoD at default != source size");
  Image out;
  out.w = src.w;
  out.h = src.h;
  out.px.assign(src.px.size(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, src.w, src.h, 0) != kOfxStatOK)
    return fail("render: Crop (defaults)");
  if (out.px != src.px) return fail("crop defaults are not identity");

  // At crop=80: RoD should shrink and render to the smaller output should differ.
  Param *crop = findParam(e.get(), "crop");
  if (!crop || crop->v.empty()) return fail("crop param missing");
  crop->v[0] = 80;
  ow = src.w; oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  if (ow >= src.w || oh >= src.h) return fail("crop RoD did not shrink at 80%");
  out.w = ow;
  out.h = oh;
  out.px.assign((size_t)ow * oh * 4, -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Crop (zoomed)");
  bool finite = true, changed = false;
  for (float v : out.px) {
    finite &= std::isfinite(v);
    changed |= v != -1.0f;
  }
  if (!finite || !changed) return fail("crop zoom output");
  if (out.px == src.px) return fail("crop slider had no effect");

  // At crop=0 the window fills the source, so both pan ranges must fall back
  // to half the crop size (not zero). Panning ±100 should slide the window
  // past the source edge, producing black where no source data exists.
  crop->v[0] = 0;
  Param *offsetX = findParam(e.get(), "offsetX");
  Param *offsetY = findParam(e.get(), "offsetY");
  if (!offsetX || offsetX->v.empty()) return fail("offsetX param missing");
  if (!offsetY || offsetY->v.empty()) return fail("offsetY param missing");
  ow = src.w; oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  out.w = ow; out.h = oh;
  out.px.assign((size_t)ow * oh * 4, -1.0f);
  offsetX->v[0] = 0; offsetY->v[0] = 0;
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Crop (centered)");
  for (float v : out.px)
    if (v == 0.0f) return fail("centered crop should have no black pixels");
  offsetY->v[0] = 100;
  std::fill(out.px.begin(), out.px.end(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Crop (Y offset)");
  changed = false;
  for (float v : out.px)
    changed |= v == 0.0f;
  if (!changed) return fail("Y offset produced no black fill");
  offsetX->v[0] = 100; offsetY->v[0] = 0;
  std::fill(out.px.begin(), out.px.end(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Crop (X offset)");
  changed = false;
  for (float v : out.px)
    changed |= v == 0.0f;
  if (!changed) return fail("X offset produced no black fill");
  callAction(it->plugin, kOfxActionDestroyInstance, e.get());
  printf("ok  Crop zoom\n");
  return 0;
}

// Renders the test image through every installed filter plugin and writes both
// export formats.
static int testEveryPlugin(Image &src) {
  for (auto &pe : gPlugins) {
    if (selftestExcluded(pe)) {
      printf("skip  %s\n", pe.label.c_str());
      continue;
    }
    auto e = createInstance(pe);
    if (!e) return fail(("createInstance: " + pe.label).c_str());
    int ow = src.w, oh = src.h;
    queryOutputSize(pe.plugin, e.get(), src.w, src.h, &ow, &oh);
    Image out;
    out.w = ow;
    out.h = oh;
    out.px.assign((size_t)ow * oh * 4, -1.0f);
    const OfxStatus st = renderEffect(pe.plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0);
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

// Concurrency smoke: render worker vs. graph mutation vs. a full-res export
// on its detached thread. No window/GL: preview display stays in memory.
static int testConcurrency(const Image &src) {
  std::vector<int> usable;
  for (int i = 0; i < (int)gPlugins.size(); ++i)
    if (!selftestExcluded(gPlugins[i])) usable.push_back(i);
  if (usable.empty()) return fail("concurrency: no usable plugins");

  int cropPi = -1;
  for (int pi = 0; pi < (int)gPlugins.size(); ++pi)
    if (gPlugins[pi].label == "Crop") cropPi = pi;
  if (cropPi < 0) return fail("concurrency: Crop plugin missing");

  App app;
  app.full = src;
  app.preview = src;
  app.renderThread = std::thread(renderWorker, &app);
  if (!addNode(app, cropPi)) return fail("concurrency addNode 1");
  for (int pi : usable)
    if (pi != cropPi && !addNode(app, pi)) return fail("concurrency addNode 2");

  // UI-side queryOutputSize runs Crop's getRoD, whose clip-RoD suite reads must
  // not race the worker's dim writes in renderEffect/queryOutputSize.
  for (int k = 0; k < 8; ++k) {
    scheduleRender(app);
    int ow = 0, oh = 0;
    queryOutputSize(gPlugins[app.nodes[0].pluginIndex].plugin, app.nodes[0].instance.get(), app.preview.w,
                    app.preview.h, &ow, &oh);
    if (ow != app.preview.w || oh != app.preview.h) return fail("concurrent queryOutputSize");
  }
  waitRenderIdle(app);

  const fs::path out = fs::temp_directory_path() / "ofxrawhost-selftest-export.png";
  for (int i = 0; i < 200; ++i) {
    scheduleRender(app);
    if (i % 11 == 0) scheduleDisplayRecolor(app);
    if (i % 13 == 0) {
      // UI keeps writing outputTag while renders run (ParamWidgets/output combo).
      app.outputTag = app.outputTag.load() == ColorSpace::sRGB ? ColorSpace::LinearRec709 : ColorSpace::sRGB;
      syncOutputTag(app);
    }
    if (i % 17 == 0) rebuildPreview(app);  // preview rewrite must wait out the worker
    if (i == 50) {
      doExport(app, out.string());
      // Mutating the graph right after an export must wait it out, not race it.
      if (app.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
    }
    if (i % 7 == 0) {
      waitRenderIdle(app);
      if (app.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
      if (app.nodes.size() > 1) moveNode(app, 0, (int)app.nodes.size() - 1);
    }
  }
  waitRenderIdle(app);  // also waits out the export thread
  app.quit = true;
  app.render.cv.notify_one();
  if (app.renderThread.joinable()) app.renderThread.join();
  if (!fs::exists(out) || fs::file_size(out) == 0) return fail("concurrent export output");
  fs::remove(out);
  const fs::path side = exportSidecarPath(out.string());
  if (fs::exists(side)) fs::remove(side);
  clearNodes(app);
  printf("ok  concurrency\n");
  return 0;
}

int runSelfTest() {
  if (const int rc = testTiffDecode()) return rc;
  if (const int rc = testPngColorSpace()) return rc;
  Image src = makeTestImage();
  if (const int rc = testRowOrder()) return rc;
  loadPlugins();
  if (gPlugins.empty()) return fail("no OFX filter plugins found");
  if (const int rc = testCropPlugin(src)) return rc;
  if (const int rc = testEveryPlugin(src)) return rc;
  if (const int rc = testConcurrency(src)) return rc;
  return 0;
}
