#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "services/Actions.h"
#include "app/AppState.h"
#include "services/ChainIO.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "engine/RenderWorker.h"
#include "imgio/ImageIO.h"
#include "imgio/NoiseProfiles.h"
#include "ofx/OfxHost.h"
#include "ofxParam.h"
#include "persist/ProjectPersist.h"
#include "services/ParamEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The bundled sensor noise profile table: it has to ship, and the lookup has to
// match the camera and interpolate an ISO the database has no shot for.
int testNoiseProfiles() {
  const std::vector<NoiseCamera> &table = noiseProfileTable();
  if (table.size() < 300) return fail("noise profiles: the bundled table did not load");
  size_t entries = 0;
  for (const NoiseCamera &camera : table) entries += camera.profiles.size();
  if (entries < 5000) return fail("noise profiles: the table is missing entries");

  NoiseProfile profile;
  if (!noiseProfileLookup("Fujifilm", "X-M1", 6400, profile)) return fail("noise profiles: X-M1 not found");
  if (std::abs(profile.a - 2.36110268483642e-05) > 1e-9) return fail("noise profiles: X-M1 a is wrong");
  if (std::abs(profile.b - 1.17390034010813e-07) > 1e-11) return fail("noise profiles: X-M1 b is wrong");

  // An ISO between two shots of the camera is interpolated between them.
  if (!noiseProfileLookup("Sony", "NEX-5T", 1000, profile)) return fail("noise profiles: NEX-5T not found");
  if (std::abs(profile.a - 1.901766432577205e-05) > 1e-9) return fail("noise profiles: interpolated a is wrong");
  if (std::abs(profile.b - 2.65520658609566e-08) > 1e-11) return fail("noise profiles: interpolated b is wrong");

  // Capitalisation the database does not use, and a model the reported string
  // carries a prefix for, both still have to match.
  if (!noiseProfileLookup("FUJIFILM", "X-M1", 6400, profile)) return fail("noise profiles: maker case hurts");
  if (!noiseProfileLookup("Canon", "Canon EOS 5D", 400, profile)) return fail("noise profiles: prefixed model hurts");

  // An ISO outside the table clamps to the closest shot the camera has.
  if (!noiseProfileLookup("Sony", "NEX-5T", 400000, profile)) return fail("noise profiles: high ISO not clamped");
  if (std::abs(profile.a - 6.62354296188816e-04) > 1e-9) return fail("noise profiles: clamped a is wrong");

  if (noiseProfileLookup("Nonesuch", "X-M1", 400, profile)) return fail("noise profiles: unknown maker matched");
  if (noiseProfileLookup("Fujifilm", "X-M9", 400, profile)) return fail("noise profiles: unknown model matched");
  printf("ok  noise profiles (%zu cameras, %zu profiles)\n", table.size(), entries);
  return 0;
}

// Adding a Denoise node to an image whose camera the host recognized must seed
// the noise model from that camera's profile, and must leave the defaults in
// place for an image that carries no camera data.
int testDenoiseSeeding() {
  auto it =
      std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Denoise (profiled)"; });
  if (it == plugins().end()) return fail("seeding: bundled Denoise plugin not found");
  const int index = (int)(it - plugins().begin());

  App app;
  app.doc.meta.hasCamera = true;
  app.doc.meta.cameraMaker = "Fujifilm";
  app.doc.meta.cameraModel = "X-M1";
  app.doc.meta.iso = 6400;
  if (!addNode(app, index)) return fail("seeding: addNode");
  Param *slope = findParam(app.chain.nodes.back().instance.get(), "noiseSlope");
  Param *offset = findParam(app.chain.nodes.back().instance.get(), "noiseOffset");
  if (!slope || slope->v.empty() || !offset || offset->v.empty()) return fail("seeding: params missing");
  if (std::abs(slope->v[0] - 2.36110268483642e-05) > 1e-9) return fail("seeding: noiseSlope not seeded");
  if (std::abs(offset->v[0] - 1.17390034010813e-07) > 1e-11) return fail("seeding: noiseOffset not seeded");
  clearNodes(app);

  // A file with no camera data keeps the parameters the plugin declares.
  App plain;
  if (!addNode(plain, index)) return fail("seeding: addNode (no camera)");
  slope = findParam(plain.chain.nodes.back().instance.get(), "noiseSlope");
  offset = findParam(plain.chain.nodes.back().instance.get(), "noiseOffset");
  if (!slope || !offset) return fail("seeding: params missing (no camera)");
  if (slope->v[0] != dprop(slope->props, kOfxParamPropDefault, 0, 0)) return fail("seeding: seeded without a camera");
  if (offset->v[0] != dprop(offset->props, kOfxParamPropDefault, 0, 0)) return fail("seeding: seeded without a camera");
  clearNodes(plain);
  printf("ok  Denoise profile seeding\n");
  return 0;
}

// Bundled Denoise (profiled) plugin: a flat frame carrying the noise the model
// describes must come back cleaner without a level shift, a clean frame must
// survive untouched, and a frame the host renders at a proxy scale must be
// denoised as thoroughly as a full sized one.
static float testNoise(size_t index) {
  // Deterministic zero-mean unit-variance noise, so the case does not depend on
  // a random engine's library implementation.
  uint32_t x = (uint32_t)(index * 2654435761u) ^ 0x9e3779b9u;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  const float uniform = (float)((double)x / 2147483648.0 - 1.0);  // [-1, 1)
  return uniform * 1.7320508f;                                    // stddev 1
}

static void frameStats(const Image &img, double &mean, double &stddev) {
  const size_t count = (size_t)img.w * img.h;
  double sum = 0.0;
  for (size_t i = 0; i < count; ++i) sum += img.px[4 * i];
  mean = sum / (double)count;
  double variance = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double d = img.px[4 * i] - mean;
    variance += d * d;
  }
  stddev = std::sqrt(variance / (double)count);
}

// M_PI is not in MSVC's <cmath> unless _USE_MATH_DEFINES precedes every include,
// so the tests carry their own constant instead of depending on a build flag.
constexpr double kPi = 3.14159265358979323846;

// Amplitude a frame still carries at the texture frequency. The noise is
// uncorrelated with the texture, so it averages out of this projection and the
// number reports how much detail came through the denoiser.
static double textureAmplitude(const Image &img, float level, double period) {
  double sum = 0.0;
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x)
      sum += ((double)img.px[4 * ((size_t)y * img.w + x)] - level) * std::sin(2.0 * kPi * x / period);
  return 2.0 * sum / (double)((size_t)img.w * img.h);
}

int testDenoisePlugin() {
  auto it =
      std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Denoise (profiled)"; });
  if (it == plugins().end()) return fail("bundled Denoise plugin not found");
  auto e = createInstance(*it);
  if (!e) return fail("createInstance: Denoise");

  Param *space = findParam(e.get(), "inputSpace");
  Param *slope = findParam(e.get(), "noiseSlope");
  Param *offset = findParam(e.get(), "noiseOffset");
  Param *strength = findParam(e.get(), "strength");
  Param *mode = findParam(e.get(), "colorMode");
  if (!space || space->v.empty()) return fail("denoise: inputSpace missing");
  if (!slope || slope->v.empty()) return fail("denoise: noiseSlope missing");
  if (!offset || offset->v.empty()) return fail("denoise: noiseOffset missing");
  if (!strength || strength->v.empty()) return fail("denoise: strength missing");
  if (!mode || mode->v.empty()) return fail("denoise: colorMode missing");

  space->v[0] = 2;  // Linear Rec.709, so the noise model is read on the samples as they are
  mode->v[0] = 0;   // RGB, the mode that smooths a flat frame hardest
  strength->v[0] = 1.0;
  const float noiseA = 3.55e-5f, noiseB = 1.07e-7f;
  slope->v[0] = noiseA;
  offset->v[0] = noiseB;

  const int w = 64, h = 48;
  const float level = 0.2f;
  const float sigma = std::sqrt(noiseA * level + noiseB);
  // A texture eight pixels wide lands in the finest wavelet bands, which is where
  // a noise level that does not match the frame shows up first.
  const float texture = 0.03f;
  const double period = 8.0;
  auto render = [&](float textureAmount, float noiseAmount, double renderScale, Image &out) {
    Image in;
    in.w = w;
    in.h = h;
    in.px.assign((size_t)w * h * 4, 1.0f);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const size_t i = (size_t)y * w + x;
        const double clean = level + textureAmount * std::sin(2.0 * kPi * x / period);
        for (size_t c = 0; c < 3; ++c)
          in.px[4 * i + c] = (float)clean + sigma * noiseAmount * testNoise(4 * i + c);
      }
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h * 4, -1.0f);
    return renderEffect(it->plugin, e.get(), in.px.data(), out.px.data(), w, h, w, h, 0, nullptr, nullptr, false,
                        renderScale);
  };

  Image out;
  double mean = 0.0, stddev = 0.0;
  if (render(0.0f, 1.0f, 1.0, out) != kOfxStatOK) return fail("render: Denoise (noisy)");
  frameStats(out, mean, stddev);
  if (stddev > 0.2 * sigma) return fail("denoise left the modelled noise in place");
  if (std::abs(mean - level) > 2e-3) return fail("denoise shifted the frame level");
  const double flatFull = stddev;

  // A frame without noise has nothing to shrink, and must come back flat.
  if (render(0.0f, 0.0f, 1.0, out) != kOfxStatOK) return fail("render: Denoise (clean)");
  frameStats(out, mean, stddev);
  if (stddev > 0.05 * sigma) return fail("denoise added structure to a flat frame");
  if (std::abs(mean - level) > 2e-3) return fail("denoise shifted a clean frame level");

  // A proxy frame is an average of several sensor pixels per output pixel, so it
  // carries proportionally less noise. The robustness of the plugin to the render
  // scale is what makes a preview show what the export will write: the leftover
  // noise must shrink by the same factor the noise did.
  if (render(0.0f, 0.5f, 0.5, out) != kOfxStatOK) return fail("render: Denoise (proxy scale)");
  frameStats(out, mean, stddev);
  if (std::abs(stddev - 0.5 * flatFull) > 0.25 * flatFull) return fail("denoise did not follow the render scale");

  // The denoiser must keep the detail a noisy frame still carries, rather than
  // smoothing everything the way a plain blur would.
  if (render(texture, 1.0f, 1.0, out) != kOfxStatOK) return fail("render: Denoise (texture)");
  const double keptFull = textureAmplitude(out, level, period);
  if (keptFull < 0.5 * texture) return fail("denoise smoothed the texture away");

  // More strength must smooth more.
  strength->v[0] = 8.0;
  if (render(texture, 1.0f, 1.0, out) != kOfxStatOK) return fail("render: Denoise (strength)");
  if (textureAmplitude(out, level, period) >= keptFull) return fail("denoise strength did not smooth further");

  callAction(it->plugin, kOfxActionDestroyInstance, e.get());
  printf("ok  Denoise (profiled)\n");
  return 0;
}

// Bundled Transform plugin: defaults must be an identity pass-through with a
// full-size RoD; the zoom slider must shrink the RoD and change the rendered
// output. Checked first so a flaky third-party plugin later in the list
// cannot mask a regression here.
int testTransformPlugin(Image &src) {
  auto it = std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == plugins().end()) return fail("bundled Transform plugin not found");
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
int testTransformLegacyMigration() {
  auto it = std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == plugins().end()) return fail("migration: bundled Transform plugin not found");

  PersistChain chain;
  PersistNode pn;
  pn.pluginIdentifier = it->plugin->pluginIdentifier;
  pn.pluginLabel = "Transform";
  pn.params["crop"] = JsonValue::makeDouble(50.0);
  pn.params["zoom"] = JsonValue::makeDouble(100.0);
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
int testAutoExposurePlugin() {
  auto it = std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Auto Exposure"; });
  if (it == plugins().end()) return fail("bundled Auto Exposure plugin not found");
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
int testPluginColorSpaceMapping() {
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

// "Reset Parameters" must return every value parameter to its OFX default, so a
// plugin that was edited returns to the state it has when it is added.
int testResetNodeParams() {
  auto it = std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == plugins().end()) return fail("reset: bundled Transform plugin not found");
  const int index = (int)(it - plugins().begin());

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
int testChainColorSpaceFlow() {
  auto it = std::find_if(plugins().begin(), plugins().end(), [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == plugins().end()) return fail("chain space: bundled Transform plugin not found");

  App app;
  if (app.gui.previewSpace != ColorSpace::sRGB) return fail("chain space: preview default is not sRGB");
  if (app.gui.exportSpace != ColorSpace::sRGB) return fail("chain space: export default is not sRGB");

  app.doc.fileSpace = ColorSpace::LinearRec709;
  app.doc.workingSpace = ColorSpace::LinearRec709;
  app.doc.inputSpace = ColorSpace::LinearRec709;
  if (!addNode(app, (int)(it - plugins().begin()))) return fail("chain space: addNode");
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
int testEveryPlugin(Image &src) {
  for (auto &pe : plugins()) {
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
