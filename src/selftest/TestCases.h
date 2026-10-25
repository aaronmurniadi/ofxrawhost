// The cases run by runSelfTest, in no particular order: Selftest.cpp owns the
// order, because some cases depend on the plugin registry being loaded.
#pragma once

#include "codecs/ImageIO.h"

// Decode, row order, and color conversion.
int testTiffDecode();
int testPngColorSpace();
int testRowOrder();
int testColorSpaceConversion();
int testPluginColorSpaceMapping();

// State files, settings, fonts, and update checking.
int testBundledFonts();
int testFontSizeMigration();
int testStateFilesAreAppLevel();
int testGlobalSettingsRoundTrip();
int testUpdateVersionOrdering();

// The bundled plugins and every installed one. src is the shared test image.
int testNoiseProfiles();
int testDenoiseSeeding();
int testDenoisePlugin();
int testTransformPlugin(Image &src);
int testTransformLegacyMigration();
int testOpenWithSidecarPrompt();
int testAutoExposurePlugin();
int testResetNodeParams();
int testChainColorSpaceFlow();
int testEveryPlugin(Image &src);

// Render worker against graph mutation and a full-resolution export.
int testConcurrency(const Image &src);
