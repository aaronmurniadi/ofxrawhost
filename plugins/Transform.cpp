#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "BundledPlugins.h"

#include "ofxsImageEffect.h"

#include <algorithm>
#include <cmath>
#include <memory>

#define kPluginName "Transform"
#define kPluginGrouping "OFX Raw Host"
#define kPluginDescription "Zoom into an aspect-ratio region, then rotate the result."
#define kPluginIdentifier "com.aaronmurniadi.ofxrawhost.transform"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 1

namespace {

constexpr double kPi = 3.14159265358979323846;

// ratio == 0 keeps the source aspect ratio.
struct AspectEntry {
  const char *label;
  double ratio;
};

// Ratios are landscape; the orientation parameter swaps width and height.
const AspectEntry kAspects[] = {
    {"Original", 0.0},
    {"1:1 (Square)", 1.0},
    {"6:5 (Photo)", 6.0 / 5.0},
    {"5:4 (Large Format)", 5.0 / 4.0},
    {"4:3 (Classic TV)", 4.0 / 3.0},
    {"1.37:1 (Academy)", 1.37},
    {"7:5 (Photo)", 7.0 / 5.0},
    {"1.43:1 (IMAX)", 1.43},
    {"3:2 (Film Landscape)", 3.0 / 2.0},
    {"16:10 (Widescreen)", 16.0 / 10.0},
    {"1.66:1 (Super 16)", 1.66},
    {"5:3 (Wide)", 5.0 / 3.0},
    {"7:4 (Wide)", 7.0 / 4.0},
    {"16:9 (Widescreen)", 16.0 / 9.0},
    {"1.85:1 (Cinema Flat)", 1.85},
    {"2:1 (Univisium)", 2.0},
    {"21:9 (Ultrawide)", 21.0 / 9.0},
    {"2.39:1 (Anamorphic)", 2.39},
    {"3:1 (Panorama)", 3.0},
    {"4:1 (Extreme Wide)", 4.0},
};

class TransformPlugin : public OFX::ImageEffect {
public:
  explicit TransformPlugin(OfxImageEffectHandle handle)
      : ImageEffect(handle) {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    aspect_ = fetchChoiceParam("aspect");
    orientation_ = fetchChoiceParam("orientation");
    zoom_ = fetchDoubleParam("zoom");
    rotate_ = fetchDoubleParam("rotate");
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
  OFX::ChoiceParam *orientation_;
  OFX::DoubleParam *zoom_;
  OFX::DoubleParam *rotate_;
  OFX::DoubleParam *offsetX_;
  OFX::DoubleParam *offsetY_;
};

void TransformPlugin::computeCropWindow(double time, double srcW, double srcH,
                                        double &x0, double &y0, double &w, double &h) {
  int choice = 0;
  aspect_->getValueAtTime(time, choice);
  if (choice < 0 || choice >= static_cast<int>(sizeof kAspects / sizeof kAspects[0]))
    choice = 0;
  double ar = kAspects[choice].ratio;
  const bool original = (ar <= 0.0);
  if (original)
    ar = srcW / srcH;

  // Original keeps the source shape and ignores orientation.
  int orient = 0;
  orientation_->getValueAtTime(time, orient);
  if (!original && orient == 1)
    ar = 1.0 / ar;

  // Zoom sets how far the crop window shrinks: 100 keeps the full source shape
  // and higher values shrink the window toward the center in both dimensions.
  const double zoomVal = std::clamp(zoom_->getValueAtTime(time), 100.0, 1000.0);
  const double scale = 100.0 / zoomVal;

  // Largest rectangle with the target aspect ratio that fits the source, scaled by the zoom.
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

void TransformPlugin::render(const OFX::RenderArguments &args) {
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

  // The crop window is already sized by the zoom slider, so it is sampled one
  // to one. The rotation center stays at the window center.
  const double scx = x0 + w * 0.5;
  const double scy = y0 + h * 0.5;
  const double sampleW = w;
  const double sampleH = h;
  const double sx0 = x0;
  const double sy0 = y0;

  const double angle = std::clamp(rotate_->getValueAtTime(args.time), -180.0, 180.0) * kPi / 180.0;
  const double cosA = std::cos(angle);
  const double sinA = std::sin(angle);

  // Magnify just enough after rotation so the axis-aligned crop window stays filled (no black wedges).
  double cover = 1.0;
  if (angle != 0.0) {
    const double c = std::abs(cosA);
    const double s = std::abs(sinA);
    cover = std::max(c + (h / w) * s, (w / h) * s + c) * (1.0 + 1e-6);
  }
  const double rodX0 = x0, rodY0 = y0, rodW = w, rodH = h;

  const double outW = static_cast<double>(rw.x2 - rw.x1);
  const double outH = static_cast<double>(rw.y2 - rw.y1);

  for (int y = rw.y1; y < rw.y2; ++y) {
    if (abort())
      break;
    float *d = static_cast<float *>(dst->getPixelAddress(rw.x1, y));
    if (!d)
      continue;

    const double ny = (y + 0.5 - rw.y1) / outH;
    const double dy = rodY0 + ny * rodH - scy;

    for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
      const double nx = (x + 0.5 - rw.x1) / outW;
      const double dx = rodX0 + nx * rodW - scx;
      // Inverse rotation of the output pixel about the crop center.
      const double ux = dx * cosA + dy * sinA;
      const double uy = -dx * sinA + dy * cosA;
      const double fx = 0.5 + ux / (w * cover);
      const double fy = 0.5 + uy / (h * cover);
      // Outside the magnified sample window (only at extreme zoom/rotation).
      if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0) {
        d[0] = d[1] = d[2] = d[3] = 0.0f;
        continue;
      }
      const double sx = sx0 + fx * sampleW;
      const double sy = sy0 + fy * sampleH;
      const double fpx = sx * rsx - 0.5 - sb.x1;
      const double fpy = sy * rsy - 0.5 - sb.y1;
      // Outside the source by more than half a pixel: this pixel is black.
      if (fpx < -0.5 || fpx > static_cast<double>(sw) - 0.5 ||
          fpy < -0.5 || fpy > static_cast<double>(sh) - 0.5) {
        d[0] = d[1] = d[2] = d[3] = 0.0f;
        continue;
      }
      // Clamp the outer half pixel to the source edge.
      const double cpx = std::clamp(fpx, 0.0, static_cast<double>(sw - 1));
      const double cpy = std::clamp(fpy, 0.0, static_cast<double>(sh - 1));
      const int ix0 = static_cast<int>(std::floor(cpx));
      const int ix1 = std::min(ix0 + 1, sw - 1);
      const int iy0 = static_cast<int>(std::floor(cpy));
      const int iy1 = std::min(iy0 + 1, sh - 1);
      const double tx = cpx - ix0;
      const double ty = cpy - iy0;
      const float *p00 = static_cast<const float *>(src->getPixelAddress(sb.x1 + ix0, sb.y1 + iy0));
      const float *p10 = static_cast<const float *>(src->getPixelAddress(sb.x1 + ix1, sb.y1 + iy0));
      const float *p01 = static_cast<const float *>(src->getPixelAddress(sb.x1 + ix0, sb.y1 + iy1));
      const float *p11 = static_cast<const float *>(src->getPixelAddress(sb.x1 + ix1, sb.y1 + iy1));
      if (!p00 || !p10 || !p01 || !p11) {
        d[0] = d[1] = d[2] = d[3] = 0.0f;
        continue;
      }
      for (int c = 0; c < 4; ++c) {
        const double top = p00[c] + tx * (p10[c] - p00[c]);
        const double bot = p01[c] + tx * (p11[c] - p01[c]);
        d[c] = static_cast<float>(top + ty * (bot - top));
      }
    }
  }
}

bool TransformPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) {
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

  // The canvas stays the crop window; rotation never changes the output size.
  rod = {x0, y0, x0 + w, y0 + h};
  return true;
}

}  // namespace

mDeclarePluginFactory(TransformPluginFactory, {}, {});

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc) {
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

void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum) {
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

  ChoiceParamDescriptor *orientation = desc.defineChoiceParam("orientation");
  orientation->setLabels("Orientation", "Orientation", "Landscape keeps the aspect ratio; Portrait swaps width and height.");
  orientation->setHint("Landscape keeps the chosen aspect ratio. Portrait swaps its width and height. The Original aspect ratio keeps the source shape and ignores this parameter.");
  orientation->setDefault(0);
  orientation->appendOption("Landscape");
  orientation->appendOption("Portrait");
  page->addChild(*orientation);

  DoubleParamDescriptor *zoom = desc.defineDoubleParam("zoom");
  zoom->setLabels("Zoom", "Zoom", "Zoom in from the source; 100 outputs the full image and higher values crop to a smaller region.");
  zoom->setHint("Zoom in from the source. 100 outputs the full image (identity). Higher values crop to a centered region that matches the aspect ratio, then sample it one to one. The output shrinks as the region shrinks, so downstream plugins process fewer pixels. At 1000 the region is 10% of the source in each dimension.");
  zoom->setDefault(100);
  zoom->setRange(100, 1000);
  zoom->setDisplayRange(100, 400);
  zoom->setIncrement(1);
  page->addChild(*zoom);

  DoubleParamDescriptor *rotate = desc.defineDoubleParam("rotate");
  rotate->setLabels("Rotate", "Rotate", "Rotate the cropped image about its center, in degrees.");
  rotate->setHint("Rotate the cropped image about its center, in degrees, from -180 to 180. The crop window stays the output size; the image scales up as needed so the frame stays filled with no black corners.");
  rotate->setDefault(0);
  rotate->setRange(-180, 180);
  rotate->setDisplayRange(-180, 180);
  rotate->setIncrement(0.01);
  page->addChild(*rotate);

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

OFX::ImageEffect *TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) {
  return new TransformPlugin(handle);
}

void registerTransformPlugin(OFX::PluginFactoryArray &ids) {
  static TransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
  ids.push_back(&p);
}
