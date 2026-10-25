// Self-test entry point: builds the shared test image and runs every case in
// the order below. The cases themselves live in the Test*.cpp modules beside
// this file, grouped by what they exercise. Run with --selftest.

#include "selftest/Selftest.h"

#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "codecs/ImageIO.h"
#include "ofx/OfxHost.h"

#include <cmath>

namespace {

// A gray ramp that gives every plugin something with a large dynamic range.
Image makeTestImage() {
  Image src;
  src.w = 64;
  src.h = 48;
  src.px.assign((size_t)src.w * src.h * 4, 1.0f);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      for (int c = 0; c < 3; ++c) src.px[((size_t)y * src.w + x) * 4 + c] = 0.18f * std::exp2((x - src.w / 2) / 8.0f);
  return src;
}

}  // namespace

// The order matters: the font and decode cases need no plugin, and the plugin
// registry is loaded once, before the cases that render through it.
int runSelfTest() {
  if (const int rc = testBundledFonts()) return rc;
  if (const int rc = testTiffDecode()) return rc;
  if (const int rc = testPngColorSpace()) return rc;
  Image src = makeTestImage();
  if (const int rc = testRowOrder()) return rc;
  if (const int rc = testGlobalSettingsRoundTrip()) return rc;
  if (const int rc = testFontSizeMigration()) return rc;
  if (const int rc = testStateFilesAreAppLevel()) return rc;
  if (const int rc = testUpdateVersionOrdering()) return rc;
  if (const int rc = testColorSpaceConversion()) return rc;
  if (const int rc = testPluginColorSpaceMapping()) return rc;
  loadPlugins();
  if (plugins().empty()) return fail("no OFX filter plugins found");
  if (const int rc = testResetNodeParams()) return rc;
  if (const int rc = testChainColorSpaceFlow()) return rc;
  if (const int rc = testTransformPlugin(src)) return rc;
  if (const int rc = testTransformLegacyMigration()) return rc;
  if (const int rc = testOpenWithSidecarPrompt()) return rc;
  if (const int rc = testNoiseProfiles()) return rc;
  if (const int rc = testAutoExposurePlugin()) return rc;
  if (const int rc = testDenoisePlugin()) return rc;
  if (const int rc = testDenoiseSeeding()) return rc;
  if (const int rc = testEveryPlugin(src)) return rc;
  if (const int rc = testConcurrency(src)) return rc;
  return 0;
}
