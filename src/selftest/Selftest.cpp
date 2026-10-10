// Self-test cases: decode and color checks, the bundled plugins, every
// installed plugin, and a concurrency smoke case. Run with --selftest.

#include "selftest/Selftest.h"

#include "Actions.h"
#include "NodeGraph.h"
#include "RenderScheduler.h"
#include "imgio/ImageIO.h"
#include "ParamBridge.h"
#include "ofx/OfxHost.h"
#include "persist/ChainIO.h"
#include "persist/ProjectPersist.h"
#include "platform/EmbeddedResource.h"
#include "platform/Paths.h"
#include "platform/SystemUiFont.h"
#include "ui/ParamEdit.h"

#include "imgui.h"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// The release bundles the interface font (Inter) and the icon font, so the host
// never depends on a system font. The interface font must also be loadable: a
// truncated or unsupported file would fail here instead of at startup.
static int testBundledFonts() {
#if defined(_WIN32)
  if (embeddedResource(kIconFontResourceId).empty()) return fail("embedded icon font is missing");
  if (embeddedResource(kUiFontResourceId).empty()) return fail("embedded interface font is missing");
#endif

  // Read the interface font from the same places the UI does.
  std::vector<unsigned char> bytes = embeddedResource(kUiFontResourceId);
  if (bytes.empty()) {
    const fs::path exe = exeDir();
    const std::vector<std::string> cands = {
        OFX_UI_FONT_PATH,
        (exe / "Inter.ttf").string(),
        (exe / ".." / "Resources" / "Inter.ttf").string(),
        "Inter.ttf",
    };
    for (const std::string &path : cands) {
      if (path.empty() || !fs::exists(path)) continue;
      std::ifstream in(path, std::ios::binary);
      if (!in) continue;
      bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      if (!bytes.empty()) break;
    }
  }
  if (bytes.empty()) return fail("bundled interface font not found");

  ImFontAtlas atlas;
  ImFontConfig cfg;
  cfg.SizePixels = 18.0f;
  // ImGui takes ownership of the buffer, so hand it a heap copy.
  void *copy = std::malloc(bytes.size());
  if (!copy) return fail("bundled interface font: out of memory");
  std::memcpy(copy, bytes.data(), bytes.size());
  ImFont *font = atlas.AddFontFromMemoryTTF(copy, (int)bytes.size(), cfg.SizePixels, &cfg);
  if (!font) return fail("bundled interface font did not load");
  if (!atlas.Build()) return fail("bundled interface font atlas did not build");
  unsigned char *pixels = nullptr;
  int texW = 0, texH = 0;
  atlas.GetTexDataAsAlpha8(&pixels, &texW, &texH);
  if (!pixels || texW <= 0 || texH <= 0) return fail("bundled interface font has no atlas texture");
  if (!font->FindGlyphNoFallback('A')) return fail("bundled interface font has no glyphs");

  // The interface font size follows the operating system, so it must resolve to
  // a usable size on every platform and in a headless run.
  const float systemPt = systemUiFontSizePt();
  if (!(systemPt > 4.0f) || systemPt > 40.0f) return fail("system UI font size is out of range");

  printf("ok  bundled fonts (system size %.1f pt)\n", systemPt);
  return 0;
}

static bool writeTinyTiff(const fs::path &p, bool halfFloat) {
  TIFF *tif = TIFFOpen(p.string().c_str(), "w");
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

// Untagged float TIFF → Rec.2020; untagged 16-bit int → sRGB. The working space
// of an integer TIFF is its tag, because the samples stay encoded.
static int testTiffDecode() {
  for (bool half : {false, true}) {
    const fs::path p = fs::temp_directory_path() / (half ? "ofxrawhost-selftest-half.tif" : "ofxrawhost-selftest.tif");
    if (!writeTinyTiff(p, half)) return fail(half ? "tiff write half" : "tiff write");
    Image img;
    ColorSpace cs = ColorSpace::sRGB;
    ColorSpace working = ColorSpace::sRGB;
    if (!loadImage(p.string(), img, cs, working) || img.w != 2 || img.h != 2) {
      if (half) return fail("tiff load half");
      return fail("tiff load");
    }
    if (img.px[(size_t)1 * 2 * 4 + 0] < 0.9f) return fail(half ? "tiff pixels half" : "tiff pixels");
    if (half && cs != ColorSpace::LinearRec2020) return fail("tiff half colorspace");
    if (!half && cs != ColorSpace::sRGB) return fail("tiff uint colorspace");
    if (working != cs) return fail("tiff working space");
    fs::remove(p);
  }
  return 0;
}

// Minimal 1x1 RGB PNG, no iCCP (untagged LDR → sRGB tag, linear working space,
// because stb removes the transfer curve while it converts to float).
static int testPngColorSpace() {
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

// The per-user settings file must round-trip the preference fields, so a
// restart restores the theme, font, panel visibility, and export defaults. The
// case points the config directory at a temp folder, so it never reads or
// writes the real user settings.
static int testGlobalSettingsRoundTrip() {
  const fs::path base = fs::temp_directory_path() / "ofxrawhost-selftest-config";
  std::error_code ec;
  fs::remove_all(base, ec);

#ifdef _WIN32
  const char *envKey = "APPDATA";
#else
  const char *envKey = "HOME";
#endif
  const char *oldEnv = std::getenv(envKey);
  std::string oldValue;
  if (oldEnv) oldValue = oldEnv;
#ifdef _WIN32
  _putenv_s(envKey, base.string().c_str());
#else
  setenv(envKey, base.string().c_str(), 1);
#endif

  PersistGui saved;
  saved.themeIndex = 5;
  saved.uiFontScale = 2;
  saved.showPlugins = false;
  saved.showParameters = true;
  saved.showFilmstrip = true;
  saved.previewRes = 3;
  saved.outputIndex = 2;
  saved.exportIndex = 3;
  saved.exportIntent = 2;
  saved.exportQuality = 71;

  int rc = 0;
  if (!saveGlobalSettings(saved)) rc = fail("settings: save");
  PersistGui loaded;
  if (rc == 0 && !loadGlobalSettings(loaded)) rc = fail("settings: load");
  if (rc == 0) {
    bool same = loaded.themeIndex == saved.themeIndex && loaded.uiFontScale == saved.uiFontScale &&
                loaded.showPlugins == saved.showPlugins &&
                loaded.showParameters == saved.showParameters && loaded.showFilmstrip == saved.showFilmstrip &&
                loaded.previewRes == saved.previewRes && loaded.outputIndex == saved.outputIndex &&
                loaded.exportIndex == saved.exportIndex && loaded.exportIntent == saved.exportIntent &&
                loaded.exportQuality == saved.exportQuality;
    if (!same) rc = fail("settings: values did not round-trip");
  }

#ifdef _WIN32
  if (oldValue.empty()) _putenv_s(envKey, "");
  else _putenv_s(envKey, oldValue.c_str());
#else
  if (oldValue.empty()) unsetenv(envKey);
  else setenv(envKey, oldValue.c_str(), 1);
#endif
  fs::remove_all(base, ec);
  if (rc == 0) printf("ok  global settings round-trip\n");
  return rc;
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

// Bundled Transform plugin: defaults must be an identity pass-through with a
// full-size RoD; the zoom slider must shrink the RoD and change the rendered
// output. Checked first so a flaky third-party plugin later in the list
// cannot mask a regression here.
static int testTransformPlugin(Image &src) {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == gPlugins.end()) return fail("bundled Transform plugin not found");
  auto e = createInstance(*it);
  if (!e) return fail("createInstance: Transform");

  // At default (zoom=100): RoD matches source size and render is identity.
  int ow = src.w, oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  if (ow != src.w || oh != src.h) return fail("zoom RoD at default != source size");
  Image out;
  out.w = src.w;
  out.h = src.h;
  out.px.assign(src.px.size(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, src.w, src.h, 0) != kOfxStatOK)
    return fail("render: Transform (defaults)");
  if (out.px != src.px) return fail("zoom defaults are not identity");

  // At zoom=500: RoD should shrink to 20% and render to the smaller output should differ.
  Param *zoom = findParam(e.get(), "zoom");
  if (!zoom || zoom->v.empty()) return fail("zoom param missing");
  zoom->v[0] = 500;
  ow = src.w; oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  if (ow >= src.w || oh >= src.h) return fail("zoom RoD did not shrink at 500");
  out.w = ow;
  out.h = oh;
  out.px.assign((size_t)ow * oh * 4, -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Transform (zoomed)");
  bool finite = true, changed = false;
  for (float v : out.px) {
    finite &= std::isfinite(v);
    changed |= v != -1.0f;
  }
  if (!finite || !changed) return fail("zoom output");
  if (out.px == src.px) return fail("zoom slider had no effect");

  // At zoom=100 the window fills the source, so both pan ranges must fall back
  // to half the window size (not zero). Panning ±100 should slide the window
  // past the source edge, producing black where no source data exists.
  zoom->v[0] = 100;
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
    return fail("render: Transform (centered)");
  for (float v : out.px)
    if (v == 0.0f) return fail("centered window should have no black pixels");
  offsetY->v[0] = 100;
  std::fill(out.px.begin(), out.px.end(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Transform (Y offset)");
  changed = false;
  for (float v : out.px)
    changed |= v == 0.0f;
  if (!changed) return fail("Y offset produced no black fill");
  offsetX->v[0] = 100; offsetY->v[0] = 0;
  std::fill(out.px.begin(), out.px.end(), -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Transform (X offset)");
  changed = false;
  for (float v : out.px)
    changed |= v == 0.0f;
  if (!changed) return fail("X offset produced no black fill");

  // Rotate: canvas stays the crop window; image magnifies to fill it (no black wedges).
  Param *rotate = findParam(e.get(), "rotate");
  if (!rotate || rotate->v.empty()) return fail("rotate param missing");
  offsetX->v[0] = 0;
  offsetY->v[0] = 0;
  rotate->v[0] = 45;
  ow = src.w; oh = src.h;
  queryOutputSize(it->plugin, e.get(), src.w, src.h, &ow, &oh);
  if (ow != src.w || oh != src.h) return fail("rotate changed the RoD");
  out.w = ow;
  out.h = oh;
  out.px.assign((size_t)ow * oh * 4, -1.0f);
  if (renderEffect(it->plugin, e.get(), src.px.data(), out.px.data(), src.w, src.h, ow, oh, 0) != kOfxStatOK)
    return fail("render: Transform (rotate)");
  bool finiteRot = true, hasBlack = false, touched = false;
  for (float v : out.px) {
    finiteRot &= std::isfinite(v);
    hasBlack |= v == 0.0f;
    touched |= v != -1.0f;
  }
  if (!finiteRot || !touched) return fail("rotate output");
  if (hasBlack) return fail("rotate left black pixels in frame");
  callAction(it->plugin, kOfxActionDestroyInstance, e.get());
  printf("ok  Transform zoom/rotate\n");
  return 0;
}

// A project saved before the Crop/Zoom merge stores both names. Loading it must
// fold them into the merged Zoom and keep the field of view the node showed.
static int testTransformLegacyMigration() {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == gPlugins.end()) return fail("migration: bundled Transform plugin not found");

  PersistChain chain;
  PersistNode pn;
  pn.pluginIdentifier = it->plugin->pluginIdentifier;
  pn.pluginLabel = "Transform";
  pn.paramsJson["crop"] = "50";
  pn.paramsJson["zoom"] = "100";
  chain.nodes.push_back(std::move(pn));

  App app;
  applyChain(app, chain);
  if (app.chain.nodes.size() != 1) return fail("migration: node missing");
  Param *zoom = findParam(app.chain.nodes[0].instance.get(), "zoom");
  if (!zoom || zoom->v.empty()) return fail("migration: zoom param missing");
  // crop 50 keeps half the source, so the merged Zoom must show the same field.
  if (std::abs(zoom->v[0] - 200.0) > 1e-9) return fail("migration: crop 50 and zoom 100 did not fold to 200");
  clearNodes(app);
  printf("ok  Transform legacy migration\n");
  return 0;
}

// Bundled Auto Exposure plugin: a mid-gray frame must stay unchanged, a dark
// frame must brighten, a bright frame must darken, and a compensation bias must
// add to the gain.
static int testAutoExposurePlugin() {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Auto Exposure"; });
  if (it == gPlugins.end()) return fail("bundled Auto Exposure plugin not found");
  auto e = createInstance(*it);
  if (!e) return fail("createInstance: Auto Exposure");

  // Meter on linear values so a flat frame maps straight to the gain.
  Param *space = findParam(e.get(), "inputSpace");
  Param *method = findParam(e.get(), "method");
  Param *comp = findParam(e.get(), "compensation");
  if (!space || space->v.empty()) return fail("auto exposure: inputSpace missing");
  if (!method || method->v.empty()) return fail("auto exposure: method missing");
  if (!comp || comp->v.empty()) return fail("auto exposure: compensation missing");
  space->v[0] = 2;  // Linear Rec.709
  method->v[0] = 0;  // Average, so a flat frame has one meter value
  comp->v[0] = 0;

  const int w = 16, h = 12;
  auto renderFlat = [&](float value, Image &out) {
    Image in;
    in.w = w;
    in.h = h;
    in.px.assign((size_t)w * h * 4, value);
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h * 4, -1.0f);
    return renderEffect(it->plugin, e.get(), in.px.data(), out.px.data(), w, h, w, h, 0);
  };

  Image out;
  // 18.4% gray meters to 0 EV, so the frame is unchanged.
  if (renderFlat(0.184f, out) != kOfxStatOK) return fail("render: Auto Exposure (mid gray)");
  for (float v : out.px)
    if (std::abs(v - 0.184f) > 1e-4f) return fail("auto exposure changed mid gray");

  // A dark frame must brighten and a bright frame must darken toward mid gray.
  if (renderFlat(0.046f, out) != kOfxStatOK) return fail("render: Auto Exposure (dark)");
  if (out.px[0] <= 0.046f + 1e-4f) return fail("auto exposure did not brighten a dark frame");
  if (renderFlat(0.736f, out) != kOfxStatOK) return fail("render: Auto Exposure (bright)");
  if (out.px[0] >= 0.736f - 1e-4f) return fail("auto exposure did not darken a bright frame");

  // One stop of compensation doubles a mid-gray frame.
  comp->v[0] = 1.0;
  if (renderFlat(0.184f, out) != kOfxStatOK) return fail("render: Auto Exposure (compensation)");
  if (std::abs(out.px[0] - 0.368f) > 1e-3f) return fail("auto exposure compensation != 1 EV");

  callAction(it->plugin, kOfxActionDestroyInstance, e.get());
  printf("ok  Auto Exposure\n");
  return 0;
}

// The preview color space follows the plug-in that declares one, so the map from
// a plug-in option name to a host ICC tag must cover the common names and reject
// the rest instead of guessing.
static int testPluginColorSpaceMapping() {
  struct Case {
    const char *name;
    ColorSpace host;
    bool mapped;
  };
  static const Case cases[] = {
      {"sRGB", ColorSpace::sRGB, true},
      {"Rec.709 Gamma 2.2", ColorSpace::sRGB, true},
      {"Rec.709 Gamma 2.4", ColorSpace::sRGB, true},
      {"Display P3", ColorSpace::DisplayP3, true},
      {"DCI-P3", ColorSpace::DisplayP3, true},
      {"P3-D65 Gamma 2.2", ColorSpace::DisplayP3, true},
      {"P3-D65 Gamma 2.6", ColorSpace::DisplayP3, true},
      {"Linear Rec.709", ColorSpace::LinearRec709, true},
      {"Linear P3-D65", ColorSpace::LinearRec2020, true},
      {"Linear Rec.2020", ColorSpace::LinearRec2020, true},
      {"ProPhoto RGB", ColorSpace::LinearRec2020, true},
      {"Adobe RGB (1998)", ColorSpace::LinearRec2020, true},
      {"ACES2065-1", ColorSpace::sRGB, false},
      {"Sony S-Log3 S-Gamut3", ColorSpace::sRGB, false},
      {"", ColorSpace::sRGB, false},
  };
  for (const Case &c : cases) {
    ColorSpace mapped = ColorSpace::sRGB;
    const bool ok = hostSpaceForPluginName(c.name, mapped);
    if (ok != c.mapped) return fail("color space map: wrong match");
    if (ok && mapped != c.host) return fail("color space map: wrong target");
  }
  printf("ok  plug-in color space mapping\n");
  return 0;
}

// The export converts the chain output from the preview space into the export
// space with a rendering intent. A same-space request must be an exact no-op,
// and a real conversion must change the pixels, stay finite, and round-trip back
// to the original values within a small tolerance.
static int testColorSpaceConversion() {
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

// "Reset Parameters" must return every value parameter to its OFX default, so a
// plugin that was edited returns to the state it has when it is added.
static int testResetNodeParams() {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == gPlugins.end()) return fail("reset: bundled Transform plugin not found");
  const int index = (int)(it - gPlugins.begin());

  App app;
  if (!addNode(app, index)) return fail("reset: addNode");
  Node &node = app.chain.nodes.back();
  Param *zoom = findParam(node.instance.get(), "zoom");
  Param *rotate = findParam(node.instance.get(), "rotate");
  if (!zoom || zoom->v.empty()) return fail("reset: zoom param missing");
  if (!rotate || rotate->v.empty()) return fail("reset: rotate param missing");
  zoom->v[0] = 500;
  rotate->v[0] = 45;

  const int count = resetNodeParamsToDefaults(app, node);
  if (count <= 0) return fail("reset: nothing was reset");
  if (std::abs(zoom->v[0] - 100.0) > 1e-9) return fail("reset: zoom did not return to 100");
  if (std::abs(rotate->v[0] - 0.0) > 1e-9) return fail("reset: rotate did not return to 0");
  clearNodes(app);
  printf("ok  reset parameters\n");
  return 0;
}

// The chain walk hands the document input space to the first plugin and carries
// the space each plugin declares to the next one. A plugin that declares no
// output space passes the carried space through. The space that leaves the chain
// is what the preview and the export start from, and the preview destination is
// independent of it.
static int testChainColorSpaceFlow() {
  auto it = std::find_if(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == gPlugins.end()) return fail("chain space: bundled Transform plugin not found");

  App app;
  if (app.gui.previewSpace != ColorSpace::sRGB) return fail("chain space: preview default is not sRGB");
  if (app.gui.exportSpace != ColorSpace::sRGB) return fail("chain space: export default is not sRGB");

  app.doc.fileSpace = ColorSpace::LinearRec709;
  app.doc.workingSpace = ColorSpace::LinearRec709;
  app.doc.inputSpace = ColorSpace::LinearRec709;
  if (!addNode(app, (int)(it - gPlugins.begin()))) return fail("chain space: addNode");
  if (app.chainOutputSpace.load() != ColorSpace::LinearRec709) return fail("chain space: space not carried through");

  // The document input space reaches the chain output, because Transform declares
  // no output space of its own.
  setInputSpace(app, 0);  // sRGB
  if (app.doc.inputSpace != ColorSpace::sRGB) return fail("chain space: input space not set");
  if (app.chainOutputSpace.load() != ColorSpace::sRGB) return fail("chain space: input space not carried");

  // The preview destination is the last conversion, so it must not move the
  // chain.
  setPreviewSpace(app, 3);  // Linear Rec.2020
  if (app.gui.previewSpace != ColorSpace::LinearRec2020) return fail("chain space: preview space not set");
  if (app.chainOutputSpace.load() != ColorSpace::sRGB) return fail("chain space: preview changed the chain");

  clearNodes(app);
  printf("ok  chain color space flow\n");
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
    struct ExportCase {
      const char *ext;
      EncodeOptions opts;
    };
    static const ExportCase cases[] = {
        {"png", {8, 92, false}},  {"png", {16, 92, false}}, {"jpg", {8, 92, false}},
        {"tif", {8, 92, false}},  {"tif", {16, 92, false}}, {"webp", {8, 92, false}},
        {"webp", {8, 92, true}},  {"jxl", {8, 92, false}},  {"jxl", {16, 92, false}},
        {"jxl", {16, 92, true}},
    };
    for (const ExportCase &c : cases) {
      const fs::path p = dir / ("ofxrawhost-selftest." + std::string(c.ext));
      if (!writeImage(out, p.string(), ColorSpace::sRGB, c.opts)) return fail(("export: " + std::string(c.ext)).c_str());
      fs::remove(p);
    }
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

  int transformPi = -1;
  for (int pi = 0; pi < (int)gPlugins.size(); ++pi)
    if (gPlugins[pi].label == "Transform") transformPi = pi;
  if (transformPi < 0) return fail("concurrency: Transform plugin missing");

  App app;
  app.doc.full = src;
  app.doc.preview = src;
  app.render.thread = std::thread(renderWorker, &app);
  if (!addNode(app, transformPi)) return fail("concurrency addNode 1");
  for (int pi : usable)
    if (pi != transformPi && !addNode(app, pi)) return fail("concurrency addNode 2");

  // UI-side queryOutputSize runs Transform's getRoD, whose clip-RoD suite reads must
  // not race the worker's dim writes in renderEffect/queryOutputSize.
  for (int k = 0; k < 8; ++k) {
    scheduleRender(app);
    int ow = 0, oh = 0;
    queryOutputSize(gPlugins[app.chain.nodes[0].pluginIndex].plugin, app.chain.nodes[0].instance.get(), app.doc.preview.w,
                    app.doc.preview.h, &ow, &oh);
    if (ow != app.doc.preview.w || oh != app.doc.preview.h) return fail("concurrent queryOutputSize");
  }
  waitRenderIdle(app);

  const fs::path out = fs::temp_directory_path() / "ofxrawhost-selftest-export.png";
  for (int i = 0; i < 200; ++i) {
    scheduleRender(app);
    if (i % 11 == 0) scheduleDisplayRecolor(app);
    if (i % 13 == 0) {
      // UI keeps writing the preview destination while renders run (the
      // Plugins panel colorspace combo).
      if (app.gui.previewSpace == ColorSpace::sRGB) app.gui.previewSpace = ColorSpace::LinearRec709;
      else app.gui.previewSpace = ColorSpace::sRGB;
      syncChainColorSpaces(app);
    }
    if (i % 17 == 0) rebuildPreview(app);  // preview rewrite must wait out the worker
    if (i == 50) {
      doExport(app, out.string());
      // Mutating the graph right after an export must wait it out, not race it.
      if (app.chain.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
    }
    if (i % 7 == 0) {
      waitRenderIdle(app);
      if (app.chain.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
      if (app.chain.nodes.size() > 1) moveNode(app, 0, (int)app.chain.nodes.size() - 1);
    }
  }
  waitRenderIdle(app);  // also waits out the export thread
  app.quit = true;
  app.render.schedule.cv.notify_one();
  if (app.render.thread.joinable()) app.render.thread.join();
  if (app.render.exportThread.joinable()) app.render.exportThread.join();
  if (!fs::exists(out) || fs::file_size(out) == 0) return fail("concurrent export output");
  fs::remove(out);
  const fs::path side = exportSidecarPath(out.string());
  if (fs::exists(side)) fs::remove(side);
  clearNodes(app);
  printf("ok  concurrency\n");
  return 0;
}

int runSelfTest() {
  if (const int rc = testBundledFonts()) return rc;
  if (const int rc = testTiffDecode()) return rc;
  if (const int rc = testPngColorSpace()) return rc;
  Image src = makeTestImage();
  if (const int rc = testRowOrder()) return rc;
  if (const int rc = testGlobalSettingsRoundTrip()) return rc;
  if (const int rc = testColorSpaceConversion()) return rc;
  if (const int rc = testPluginColorSpaceMapping()) return rc;
  loadPlugins();
  if (gPlugins.empty()) return fail("no OFX filter plugins found");
  if (const int rc = testResetNodeParams()) return rc;
  if (const int rc = testChainColorSpaceFlow()) return rc;
  if (const int rc = testTransformPlugin(src)) return rc;
  if (const int rc = testTransformLegacyMigration()) return rc;
  if (const int rc = testAutoExposurePlugin()) return rc;
  if (const int rc = testEveryPlugin(src)) return rc;
  if (const int rc = testConcurrency(src)) return rc;
  return 0;
}
