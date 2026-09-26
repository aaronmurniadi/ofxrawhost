// Minimal still-image OpenFX host: decode RAW/any ImageIO image with Core Image,
// run one OFX filter plugin (CPU buffers, float RGBA), preview, export.

#import "Controller.h"

#include "ImageIO.h"
#include "OfxHost.h"

#include <cmath>

static int fail(const char *msg) {
  fprintf(stderr, "selftest FAILED: %s\n", msg);
  return 1;
}

// Checks Core Image row order and renders a gray ramp through every installed filter plugin.
static int selfTest() {
  CIContext *ctx = [CIContext contextWithOptions:@{kCIContextWorkingFormat : @(kCIFormatRGBAf)}];
  CIImage *topRed = [[[CIImage imageWithColor:CIColor.redColor] imageByCroppingToRect:CGRectMake(0, 4, 8, 4)]
      imageByCompositingOverImage:[[CIImage imageWithColor:CIColor.blackColor] imageByCroppingToRect:CGRectMake(0, 0, 8, 8)]];
  int w = 0, h = 0;
  Pixels px = renderSource(ctx, topRed, 0, w, h);
  if (w != 8 || h != 8 || (*px)[0] > 0.1f || (*px)[(size_t)7 * 8 * 4] < 0.5f) return fail("source rows are not bottom-up");

  loadPlugins();
  if (gPlugins.empty()) return fail("no OFX filter plugins found");
  for (auto &pe : gPlugins) {
    auto e = createInstance(pe);
    if (!e) return fail(("createInstance: " + pe.label).c_str());
    w = 64;
    h = 48;
    std::vector<float> src((size_t)w * h * 4, 1.0f), out(src.size(), -1.0f);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 3; ++c) src[((size_t)y * w + x) * 4 + c] = 0.18f * std::exp2((x - w / 2) / 8.0f);
    const OfxStatus st = renderEffect(pe.plugin, e.get(), src.data(), out.data(), w, h, 0);
    callAction(pe.plugin, kOfxActionDestroyInstance, e.get());
    if (st != kOfxStatOK) return fail(("render: " + pe.label).c_str());
    bool finite = true, touched = false;
    for (float v : out) {
      finite &= std::isfinite(v);
      touched |= v != -1.0f;
    }
    if (!finite || !touched) return fail(("output: " + pe.label).c_str());
    CGImageRef cg = makeCGImage(out, w, h, kCGColorSpaceSRGB);
    bool written = true;
    for (NSString *ext in @[ @"tif", @"png", @"jpg", @"exr" ])
      written &= writeImage(cg, [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
                                                                                  [@"ofxrawhost-selftest." stringByAppendingString:ext]]]);
    CGImageRelease(cg);
    if (!written) return fail("export");
    printf("ok  %s\n", pe.label.c_str());
  }
  return 0;
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selfTest();
    NSApplication *app = [NSApplication sharedApplication];
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    Controller *controller = [Controller new];
    app.delegate = controller;
    [app run];
  }
  return 0;
}
