#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "ofxsImageEffect.h"

#include <algorithm>
#include <cmath>
#include <memory>

#define kPluginName "Crop"
#define kPluginGrouping "OFX Raw Host"
#define kPluginDescription "Crop to an aspect ratio with adjustable crop amount and center offsets."
#define kPluginIdentifier "com.aaronmurniadi.ofxrawhost.crop"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

namespace {

// ratio == 0 keeps the source aspect ratio.
struct AspectEntry {
  const char *label;
  double ratio;
};

const AspectEntry kAspects[] = {
    {"Original", 0.0},
    {"1:1 (Square)", 1.0},
    {"4:5 (Portrait)", 4.0 / 5.0},
    {"3:4 (Portrait)", 3.0 / 4.0},
    {"9:16 (Vertical Video)", 9.0 / 16.0},
    {"16:9 (Widescreen)", 16.0 / 9.0},
    {"4:3 (Classic TV)", 4.0 / 3.0},
    {"3:2 (Film Landscape)", 3.0 / 2.0},
    {"2:3 (Film Portrait)", 2.0 / 3.0},
    {"1.85:1 (Cinema Flat)", 1.85},
    {"2.39:1 (Anamorphic)", 2.39},
    {"21:9 (Ultrawide)", 21.0 / 9.0},
};

class CropPlugin : public OFX::ImageEffect {
public:
  explicit CropPlugin(OfxImageEffectHandle handle)
      : ImageEffect(handle) {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    aspect_ = fetchChoiceParam("aspect");
    crop_ = fetchDoubleParam("crop");
    offsetX_ = fetchDoubleParam("offsetX");
    offsetY_ = fetchDoubleParam("offsetY");
  }

  void render(const OFX::RenderArguments &args) override;
  // Override the default RoD (union of input clip RoDs) so the output shrinks
  // to the cropped region. This lets downstream plugins in the chain process
  // fewer pixels when the crop sits at the front.
  bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) override;

private:
  // Computes the crop window {x0, y0, x0+w, y0+h} in source canonical coordinates.
  void computeCropWindow(double time, double srcW, double srcH,
                         double &x0, double &y0, double &w, double &h);

  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::ChoiceParam *aspect_;
  OFX::DoubleParam *crop_;
  OFX::DoubleParam *offsetX_;
  OFX::DoubleParam *offsetY_;
};

void CropPlugin::computeCropWindow(double time, double srcW, double srcH,
                                   double &x0, double &y0, double &w, double &h) {
  int choice = 0;
  aspect_->getValueAtTime(time, choice);
  if (choice < 0 || choice >= static_cast<int>(sizeof kAspects / sizeof kAspects[0]))
    choice = 0;
  double ar = kAspects[choice].ratio;
  if (ar <= 0.0)
    ar = srcW / srcH;

  const double cropVal = std::clamp(crop_->getValueAtTime(time), 0.0, 100.0);
  const double scale = std::max(1.0 - cropVal / 100.0, 0.02);

  // Largest rectangle with the target aspect ratio that fits the source, scaled by the crop slider.
  const double fitW = (ar >= srcW / srcH) ? srcW : srcH * ar;
  const double fitH = fitW / ar;
  w = fitW * scale;
  h = fitH * scale;

  // Offsets pan the crop center. When the crop window is smaller than the
  // source, the range is the available slack (current behaviour). When the
  // crop fills or exceeds the source in a dimension, the range falls back to
  // half the crop size so the offset still has an effect — the window can
  // slide beyond the source edge, with out-of-bounds areas rendered as black.
  const double offX = std::clamp(offsetX_->getValueAtTime(time), -100.0, 100.0) / 100.0;
  const double offY = std::clamp(offsetY_->getValueAtTime(time), -100.0, 100.0) / 100.0;
  const double panX = (srcW > w) ? (srcW - w) * 0.5 : w * 0.5;
  const double panY = (srcH > h) ? (srcH - h) * 0.5 : h * 0.5;
  const double cx = srcW * 0.5 + offX * panX;
  const double cy = srcH * 0.5 + offY * panY;
  x0 = cx - w * 0.5;
  y0 = cy - h * 0.5;
}

void CropPlugin::render(const OFX::RenderArguments &args) {
  std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  std::unique_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
  if (!dst || !src)
    return;

  if (dst->getPixelDepth() != OFX::eBitDepthFloat || src->getPixelDepth() != OFX::eBitDepthFloat ||
      dst->getPixelComponents() != OFX::ePixelComponentRGBA || src->getPixelComponents() != OFX::ePixelComponentRGBA)
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);

  const OfxRectI &sb = src->getBounds();
  const OfxRectI &rw = args.renderWindow;
  const int sw = sb.x2 - sb.x1;
  const int sh = sb.y2 - sb.y1;
  const double rsx = std::max(args.renderScale.x, 1e-9);
  const double rsy = std::max(args.renderScale.y, 1e-9);
  // Source size in canonical units (bounds are pixel units, canonical = pixel / renderScale).
  const double srcW = sw / rsx;
  const double srcH = sh / rsy;
  if (sw <= 0 || sh <= 0 || srcW <= 0 || srcH <= 0 || rw.x2 <= rw.x1 || rw.y2 <= rw.y1)
    return;

  double x0, y0, w, h;
  computeCropWindow(args.time, srcW, srcH, x0, y0, w, h);

  const double outW = static_cast<double>(rw.x2 - rw.x1);
  const double outH = static_cast<double>(rw.y2 - rw.y1);

  for (int y = rw.y1; y < rw.y2; ++y) {
    if (abort())
      break;
    float *d = static_cast<float *>(dst->getPixelAddress(rw.x1, y));
    if (!d)
      continue;

    const double ny = (y + 0.5 - rw.y1) / outH;
    const double sy = y0 + ny * h;
    const double fyRaw = sy * rsy - 0.5 - sb.y1;
    // Y outside the source: the entire output row is black.
    if (fyRaw < 0.0 || fyRaw > static_cast<double>(sh - 1)) {
      for (int x = rw.x1; x < rw.x2; ++x, d += 4)
        d[0] = d[1] = d[2] = d[3] = 0.0f;
      continue;
    }
    const int iy0 = static_cast<int>(std::floor(fyRaw));
    const int iy1 = std::min(iy0 + 1, sh - 1);
    const double ty = fyRaw - iy0;
    const float *r0 = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + iy0));
    const float *r1 = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + iy1));
    if (!r0 || !r1)
      continue;

    for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
      const double nx = (x + 0.5 - rw.x1) / outW;
      const double sx = x0 + nx * w;
      const double fxRaw = sx * rsx - 0.5 - sb.x1;
      // X outside the source: this pixel is black.
      if (fxRaw < 0.0 || fxRaw > static_cast<double>(sw - 1)) {
        d[0] = d[1] = d[2] = d[3] = 0.0f;
        continue;
      }
      const int ix0 = static_cast<int>(std::floor(fxRaw));
      const int ix1 = std::min(ix0 + 1, sw - 1);
      const double tx = fxRaw - ix0;
      const float *p00 = r0 + ix0 * 4;
      const float *p10 = r0 + ix1 * 4;
      const float *p01 = r1 + ix0 * 4;
      const float *p11 = r1 + ix1 * 4;
      for (int c = 0; c < 4; ++c) {
        const double top = p00[c] + tx * (p10[c] - p00[c]);
        const double bot = p01[c] + tx * (p11[c] - p01[c]);
        d[c] = static_cast<float>(top + ty * (bot - top));
      }
    }
  }
}

bool CropPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) {
  // Source clip RoD is in canonical coordinates; with this host's renderScale {1,1}
  // canonical equals pixel, so the crop window maps directly to output dimensions.
  OfxRectD srcRod;
  try {
    srcRod = srcClip_->getRegionOfDefinition(args.time);
  } catch (...) {
    return false;
  }
  const double srcW = srcRod.x2 - srcRod.x1;
  const double srcH = srcRod.y2 - srcRod.y1;
  if (srcW <= 0 || srcH <= 0)
    return false;

  double x0, y0, w, h;
  computeCropWindow(args.time, srcW, srcH, x0, y0, w, h);
  rod = {x0, y0, x0 + w, y0 + h};
  return true;
}

}  // namespace

mDeclarePluginFactory(CropPluginFactory, {}, {});

void CropPluginFactory::describe(OFX::ImageEffectDescriptor &desc) {
  desc.setLabels(kPluginName, kPluginName, kPluginName);
  desc.setPluginGrouping(kPluginGrouping);
  desc.setPluginDescription(kPluginDescription);
  desc.addSupportedContext(OFX::eContextFilter);
  desc.addSupportedBitDepth(OFX::eBitDepthFloat);
  desc.setSingleInstance(false);
  desc.setHostFrameThreading(false);
  desc.setSupportsMultiResolution(true);
  desc.setSupportsTiles(false);
  desc.setTemporalClipAccess(false);
  desc.setRenderTwiceAlways(false);
  desc.setSupportsMultipleClipPARs(false);
}

void CropPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum) {
  using namespace OFX;

  ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
  srcClip->addSupportedComponent(ePixelComponentRGBA);
  srcClip->setTemporalClipAccess(false);
  srcClip->setSupportsTiles(false);
  srcClip->setIsMask(false);

  ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
  dstClip->addSupportedComponent(ePixelComponentRGBA);
  dstClip->setSupportsTiles(false);

  PageParamDescriptor *page = desc.definePageParam("Controls");

  ChoiceParamDescriptor *aspect = desc.defineChoiceParam("aspect");
  aspect->setLabels("Aspect Ratio", "Aspect", "Aspect ratio of the crop window; Original keeps the source shape.");
  aspect->setHint("Aspect ratio of the crop window. Original keeps the source shape.");
  aspect->setDefault(0);
  for (const AspectEntry &a : kAspects)
    aspect->appendOption(a.label);
  page->addChild(*aspect);

  DoubleParamDescriptor *crop = desc.defineDoubleParam("crop");
  crop->setLabels("Crop", "Crop", "Amount to crop in, in percent: 0 outputs the full image.");
  crop->setHint("Amount to crop in, in percent. 0 outputs the full image (identity). Higher values crop to a centered region matching the aspect ratio, shrinking the output so downstream plugins process fewer pixels. At 100 the region is 2% of the source.");
  crop->setDefault(0);
  crop->setRange(0, 100);
  crop->setDisplayRange(0, 100);
  crop->setIncrement(1);
  page->addChild(*crop);

  DoubleParamDescriptor *offsetX = desc.defineDoubleParam("offsetX");
  offsetX->setLabels("Offset X", "Offset X", "Pan the crop window horizontally; -100 moves it fully left, 100 fully right.");
  offsetX->setHint("Pan the crop window horizontally. -100 moves it fully left, 0 centers it, 100 fully right. When the crop fills the source width the window may slide past the edge; out-of-bounds areas are black.");
  offsetX->setDefault(0);
  offsetX->setRange(-100, 100);
  offsetX->setDisplayRange(-100, 100);
  offsetX->setIncrement(1);
  page->addChild(*offsetX);

  DoubleParamDescriptor *offsetY = desc.defineDoubleParam("offsetY");
  offsetY->setLabels("Offset Y", "Offset Y", "Pan the crop window vertically; -100 moves it fully down, 100 fully up.");
  offsetY->setHint("Pan the crop window vertically. -100 moves it fully down, 0 centers it, 100 fully up. When the crop fills the source height the window may slide past the edge; out-of-bounds areas are black.");
  offsetY->setDefault(0);
  offsetY->setRange(-100, 100);
  offsetY->setDisplayRange(-100, 100);
  offsetY->setIncrement(1);
  page->addChild(*offsetY);
}

OFX::ImageEffect *CropPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) {
  return new CropPlugin(handle);
}

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids) {
  static CropPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
  ids.push_back(&p);
}

}  // namespace Plugin
}  // namespace OFX
