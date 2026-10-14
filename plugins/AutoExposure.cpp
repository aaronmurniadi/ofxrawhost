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
#include <vector>

#define kPluginName "Auto Exposure"
#define kPluginGrouping "OFX Raw Host"
#define kPluginDescription "Meter the frame brightness and apply an exposure gain so the metered level lands on 18.4% gray."
#define kPluginIdentifier "com.aaronmurniadi.ofxrawhost.autoexposure"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

namespace {

constexpr double kPi = 3.14159265358979323846;

// The exposure target. 18.4% gray is the usual middle-gray anchor.
constexpr double kMidGray = 0.184;

// Gain clamp. Ten stops each way covers any metered frame without blowing up.
constexpr double kMaxEv = 10.0;

// Choice order of the meter parameter, and of the input color space parameter.
enum MeterMethod { kAverage = 0, kMedian, kCenterWeighted, kPartial, kMatrix, kMultiZone, kHighlightWeighted };

enum InputSpace { kSrgb = 0, kDisplayP3, kLinearRec709, kLinearRec2020 };

// sRGB and Display P3 share the sRGB transfer curve. Rec.709 and Rec.2020 are
// already linear in this host.
bool spaceUsesSrgbCurve(int space) { return space == kSrgb || space == kDisplayP3; }

double decodeToLinear(double c, bool srgbCurve) {
  if (!srgbCurve) return c;
  if (c <= 0.04045) return c / 12.92;
  return std::pow((c + 0.055) / 1.055, 2.4);
}

double encodeFromLinear(double c, bool srgbCurve) {
  if (!srgbCurve) return c;
  if (c <= 0.0031308) return c * 12.92;
  return 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// Relative luminance coefficients per color space (D65).
void luminanceWeights(int space, double &wr, double &wg, double &wb) {
  if (space == kLinearRec2020) {
    wr = 0.2627;
    wg = 0.6780;
    wb = 0.0593;
    return;
  }
  if (space == kDisplayP3) {
    wr = 0.2289746;
    wg = 0.6917385;
    wb = 0.0792869;
    return;
  }
  // sRGB and Linear Rec.709 share primaries.
  wr = 0.2126;
  wg = 0.7152;
  wb = 0.0722;
}

// Measures the source and returns the mid-gray exposure target for it. Large
// frames are sampled on a stride so the meter stays cheap. The center-weighted
// and multi-zone methods mirror a camera meter.
bool meterFrame(const OFX::Image *src, const OfxRectI &bounds, int sw, int sh, int method, int space, double &out) {
  double yr = 0.0, yg = 0.0, yb = 0.0;
  luminanceWeights(space, yr, yg, yb);
  const bool srgbCurve = spaceUsesSrgbCurve(space);

  // Normalized coordinates put the long edge across [-0.5, 0.5].
  const double maxDim = (double)std::max(sw, sh);
  const double xScale = sw / maxDim;
  const double yScale = sh / maxDim;

  const long long total = (long long)sw * sh;
  long long step = 1;
  if (total > 900000) {
    step = (long long)std::sqrt((double)total / 900000.0);
    if (step < 1) step = 1;
  }

  std::vector<double> samples;
  if (method == kMedian) samples.reserve((size_t)(total / (step * step)) + 1);

  double sum = 0.0, count = 0.0;
  double centerSum = 0.0, centerWeight = 0.0;
  double partialSum = 0.0, partialCount = 0.0;
  double highlightNum = 0.0, highlightDen = 0.0;
  double zoneSum[3] = {0.0, 0.0, 0.0};
  double zoneCount[3] = {0.0, 0.0, 0.0};
  double cellSum[25] = {0.0};
  double cellCount[25] = {0.0};

  for (int y = 0; y < sh; y += (int)step) {
    const double ny = ((double)y / sh - 0.5) * yScale;
    for (int x = 0; x < sw; x += (int)step) {
      const float *p = static_cast<const float *>(src->getPixelAddress(bounds.x1 + x, bounds.y1 + y));
      if (!p) continue;
      const double r = decodeToLinear(p[0], srgbCurve);
      const double g = decodeToLinear(p[1], srgbCurve);
      const double b = decodeToLinear(p[2], srgbCurve);
      const double y = yr * r + yg * g + yb * b;
      if (!std::isfinite(y)) continue;

      sum += y;
      count += 1.0;
      const double nx = ((double)x / sw - 0.5) * xScale;

      if (method == kMedian) {
        samples.push_back(y);
      } else if (method == kCenterWeighted) {
        const double w = std::exp(-(nx * nx + ny * ny) / (2.0 * 0.2 * 0.2));
        centerSum += y * w;
        centerWeight += w;
      } else if (method == kPartial) {
        const double radius = std::sqrt(nx * nx + ny * ny);
        if (radius < 0.15) {
          partialSum += y;
          partialCount += 1.0;
        }
      } else if (method == kMatrix) {
        const int col = (int)std::min<long long>((long long)x * 5 / sw, 4);
        const int row = (int)std::min<long long>((long long)y * 5 / sh, 4);
        cellSum[row * 5 + col] += y;
        cellCount[row * 5 + col] += 1.0;
      } else if (method == kMultiZone) {
        const double radius = std::sqrt(nx * nx + ny * ny);
        if (radius < 0.05) {
          zoneSum[0] += y;
          zoneCount[0] += 1.0;
        } else if (radius < 0.25) {
          zoneSum[1] += y;
          zoneCount[1] += 1.0;
        } else if (radius < 0.5) {
          zoneSum[2] += y;
          zoneCount[2] += 1.0;
        }
      } else if (method == kHighlightWeighted) {
        const double w = y * y;
        highlightNum += y * w;
        highlightDen += w;
      }
    }
  }

  if (count <= 0.0) return false;

  switch (method) {
    case kAverage:
      out = sum / count;
      return true;
    case kMedian: {
      if (samples.empty()) return false;
      const size_t mid = samples.size() / 2;
      std::nth_element(samples.begin(), samples.begin() + mid, samples.end());
      out = samples[mid];
      return true;
    }
    case kCenterWeighted:
      if (centerWeight <= 0.0) return false;
      out = centerSum / centerWeight;
      return true;
    case kPartial:
      if (partialCount <= 0.0) {
        out = sum / count;
        return true;
      }
      out = partialSum / partialCount;
      return true;
    case kMatrix: {
      double weighted = 0.0, weightTotal = 0.0;
      for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
          const int i = row * 5 + col;
          if (cellCount[i] <= 0.0) continue;
          const double dx = ((double)col - 2.0) / 2.0;
          const double dy = ((double)row - 2.0) / 2.0;
          const double dist = std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0);
          const double w = 0.5 * (1.0 + std::cos(kPi * dist));
          weighted += w * (cellSum[i] / cellCount[i]);
          weightTotal += w;
        }
      }
      if (weightTotal <= 0.0) return false;
      out = weighted / weightTotal;
      return true;
    }
    case kMultiZone: {
      static const double ringWeight[3] = {0.50, 0.30, 0.20};
      double weighted = 0.0, weightTotal = 0.0;
      for (int i = 0; i < 3; ++i) {
        if (zoneCount[i] <= 0.0) continue;
        weighted += ringWeight[i] * (zoneSum[i] / zoneCount[i]);
        weightTotal += ringWeight[i];
      }
      if (weightTotal <= 0.0) {
        out = sum / count;
        return true;
      }
      out = weighted / weightTotal;
      return true;
    }
    case kHighlightWeighted:
      if (highlightDen <= 1e-12) {
        out = sum / count;
        return true;
      }
      out = highlightNum / highlightDen;
      return true;
    default:
      return false;
  }
}

class AutoExposurePlugin : public OFX::ImageEffect {
public:
  explicit AutoExposurePlugin(OfxImageEffectHandle handle)
      : ImageEffect(handle) {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    method_ = fetchChoiceParam("method");
    compensation_ = fetchDoubleParam("compensation");
    inputSpace_ = fetchChoiceParam("inputSpace");
  }

  void render(const OFX::RenderArguments &args) override;

private:
  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::ChoiceParam *method_;
  OFX::DoubleParam *compensation_;
  OFX::ChoiceParam *inputSpace_;
};

void AutoExposurePlugin::render(const OFX::RenderArguments &args) {
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
  if (sw <= 0 || sh <= 0 || rw.x2 <= rw.x1 || rw.y2 <= rw.y1)
    return;

  int method = kCenterWeighted;
  method_->getValueAtTime(args.time, method);
  if (method < kAverage || method > kHighlightWeighted) method = kCenterWeighted;

  int space = kLinearRec709;
  inputSpace_->getValueAtTime(args.time, space);
  if (space < kSrgb || space > kLinearRec2020) space = kLinearRec709;

  const double compensation = std::clamp(compensation_->getValueAtTime(args.time), -kMaxEv, kMaxEv);

  // Meter the frame, then solve for the gain that lands the meter on mid gray.
  // A frame that meters at zero or fails to meter falls back to the bias alone.
  double measured = 0.0;
  double ev = compensation;
  if (meterFrame(src.get(), sb, sw, sh, method, space, measured) && measured > 0.0 && std::isfinite(measured))
    ev = -std::log2(measured / kMidGray) + compensation;
  if (!std::isfinite(ev)) ev = 0.0;
  const double gain = std::exp2(std::clamp(ev, -kMaxEv, kMaxEv));

  // The gain is a linear-light scale, so encoded sources decode, scale, and re-encode.
  const bool srgbCurve = spaceUsesSrgbCurve(space);

  for (int y = rw.y1; y < rw.y2; ++y) {
    if (abort())
      break;
    float *d = static_cast<float *>(dst->getPixelAddress(rw.x1, y));
    if (!d)
      continue;
    for (int x = rw.x1; x < rw.x2; ++x, d += 4) {
      const float *p = static_cast<const float *>(src->getPixelAddress(x, y));
      if (!p) {
        d[0] = d[1] = d[2] = d[3] = 0.0f;
        continue;
      }
      d[0] = (float)encodeFromLinear(decodeToLinear(p[0], srgbCurve) * gain, srgbCurve);
      d[1] = (float)encodeFromLinear(decodeToLinear(p[1], srgbCurve) * gain, srgbCurve);
      d[2] = (float)encodeFromLinear(decodeToLinear(p[2], srgbCurve) * gain, srgbCurve);
      d[3] = p[3];
    }
  }
}

}  // namespace

mDeclarePluginFactory(AutoExposurePluginFactory, {}, {});

void AutoExposurePluginFactory::describe(OFX::ImageEffectDescriptor &desc) {
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

void AutoExposurePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum) {
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

  ChoiceParamDescriptor *method = desc.defineChoiceParam("method");
  method->setLabels("Metering", "Metering", "How the frame brightness is measured.");
  method->setHint("How the frame brightness is measured before the gain is solved. Average and Median use the whole frame. Center Weighted, Partial, Matrix, and Multi Zone weight the center like a camera meter. Highlight Weighted favors the bright pixels. The default is Center Weighted.");
  method->setDefault(kCenterWeighted);
  method->appendOption("Average");
  method->appendOption("Median");
  method->appendOption("Center Weighted");
  method->appendOption("Partial");
  method->appendOption("Matrix");
  method->appendOption("Multi Zone");
  method->appendOption("Highlight Weighted");
  page->addChild(*method);

  DoubleParamDescriptor *compensation = desc.defineDoubleParam("compensation");
  compensation->setLabels("Exposure Compensation", "Comp", "Bias added to the measured exposure, in EV.");
  compensation->setHint("Bias added to the measured exposure, in EV. Negative values darken the image, positive values brighten it. The default is 0.");
  compensation->setDefault(0);
  compensation->setRange(-kMaxEv, kMaxEv);
  compensation->setDisplayRange(-5, 5);
  compensation->setIncrement(0.01);
  page->addChild(*compensation);

  ChoiceParamDescriptor *inputSpace = desc.defineChoiceParam("inputSpace");
  inputSpace->setLabels("Input Color Space", "Input", "Color space of the source pixels.");
  inputSpace->setHint("Color space of the source pixels. The host sets this from the opened image. The meter decodes the transfer curve and uses the matching luminance weights.");
  inputSpace->setDefault(kLinearRec709);
  inputSpace->appendOption("sRGB");
  inputSpace->appendOption("Display P3");
  inputSpace->appendOption("Linear Rec.709");
  inputSpace->appendOption("Linear Rec.2020");
  page->addChild(*inputSpace);
}

OFX::ImageEffect *AutoExposurePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) {
  return new AutoExposurePlugin(handle);
}

void registerAutoExposurePlugin(OFX::PluginFactoryArray &ids) {
  static AutoExposurePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
  ids.push_back(&p);
}
