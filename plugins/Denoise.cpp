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
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

#define kPluginName "Denoise (profiled)"
#define kPluginGrouping "OFX Raw Host"
#define kPluginDescription "Denoise with a Poisson-Gaussian noise model: a variance stabilizing transform, edge-aware a-trous wavelets, and BayesShrink thresholds."
#define kPluginIdentifier "com.aaronmurniadi.ofxrawhost.denoiseprofiled"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

// A port of the wavelets mode of darktable's denoise (profiled) module. The
// sensor noise is modelled per channel as
//
//     V(X) = a * X + b
//
// that is a Poisson part (photons, so the variance grows with the signal) plus a
// Gaussian floor (read noise). The module first applies the generalized Anscombe
// transform, which turns that signal dependent noise into noise of unit
// variance, and only then runs an edge-aware a-trous wavelet shrinkage on the
// stabilized image. Because the thresholds are computed on unit-variance data,
// the same amount of smoothing happens in the shadows and in the highlights, and
// a and b are the only things that describe the camera. The transform is
// inverted at the end.

namespace {

// Number of wavelet bands. darktable tunes the threshold of every band through a
// frequency curve with seven points per channel.
constexpr int kBands = 7;

// The fulcrum that separates the shadows from the rest of the image. Applying
// the transform to this value gives the same result whatever the shadows
// parameter is, which is what makes the "preserve shadows" slider behave the way
// it does.
constexpr float kPFulcrum = 0.05f;

// darktable's frequency curve, fixed at its default force of 0.5 for every band
// and channel. The host has no curve widget to edit it with.
constexpr float kBandForce = 0.5f;

// The B3 spline a-trous filter, as in darktable's common/eaw.c.
const float kFilter[25] = {
    1.0f / 256.0f, 4.0f / 256.0f, 6.0f / 256.0f, 4.0f / 256.0f, 1.0f / 256.0f,
    4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f, 4.0f / 256.0f,
    6.0f / 256.0f, 24.0f / 256.0f, 36.0f / 256.0f, 24.0f / 256.0f, 6.0f / 256.0f,
    4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f, 4.0f / 256.0f,
    1.0f / 256.0f, 4.0f / 256.0f, 6.0f / 256.0f, 4.0f / 256.0f, 1.0f / 256.0f};

// Choice order of the input color space parameter, matching the other bundled
// plugins.
enum InputSpace { kSrgb = 0, kDisplayP3, kLinearRec709, kLinearRec2020 };

// Choice order of the wavelet color mode, and of darktable's own parameter.
enum ColorMode { kRgbMode = 0, kY0U0V0Mode = 1 };

// sRGB and Display P3 share the sRGB transfer curve. Rec.709 and Rec.2020 are
// already linear in this host.
bool spaceUsesSrgbCurve(int space) { return space == kSrgb || space == kDisplayP3; }

float decodeToLinear(float c, bool srgbCurve) {
  if (!srgbCurve) return c;
  if (c <= 0.04045f) return c / 12.92f;
  return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float encodeFromLinear(float c, bool srgbCurve) {
  if (!srgbCurve) return c;
  if (c <= 0.0031308f) return c * 12.92f;
  return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// A 3x3 color matrix.
struct Mat3 {
  float m[3][3];
};

void matVec(const Mat3 &mat, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = mat.m[r][0] * in[0] + mat.m[r][1] * in[1] + mat.m[r][2] * in[2];
}

// The standard cofactor inverse; false when the matrix is singular.
bool invert3x3(const Mat3 &in, Mat3 &out) {
  const float bigA = in.m[1][1] * in.m[2][2] - in.m[1][2] * in.m[2][1];
  const float bigB = -in.m[1][0] * in.m[2][2] + in.m[1][2] * in.m[2][0];
  const float bigC = in.m[1][0] * in.m[2][1] - in.m[1][1] * in.m[2][0];
  const float bigD = -in.m[0][1] * in.m[2][2] + in.m[0][2] * in.m[2][1];
  const float bigE = in.m[0][0] * in.m[2][2] - in.m[0][2] * in.m[2][0];
  const float bigF = -in.m[0][0] * in.m[2][1] + in.m[0][1] * in.m[2][0];
  const float bigG = in.m[0][1] * in.m[1][2] - in.m[0][2] * in.m[1][1];
  const float bigH = -in.m[0][0] * in.m[1][2] + in.m[0][2] * in.m[1][0];
  const float bigI = in.m[0][0] * in.m[1][1] - in.m[0][1] * in.m[1][0];

  const float det = in.m[0][0] * bigA + in.m[0][1] * bigB + in.m[0][2] * bigC;
  if (det == 0.0f) return false;

  out.m[0][0] = bigA / det;
  out.m[0][1] = bigD / det;
  out.m[0][2] = bigG / det;
  out.m[1][0] = bigB / det;
  out.m[1][1] = bigE / det;
  out.m[1][2] = bigH / det;
  out.m[2][0] = bigC / det;
  out.m[2][1] = bigF / det;
  out.m[2][2] = bigI / det;
  return true;
}

// Conversion into the Y0U0V0 space of "From Theory to Practice, a Tour of Image
// Denoising". Y0 sums the channels, so its signal adds up while its noise
// partially cancels, and U0 and V0 are the color differences, which carry
// almost no signal and can be shrunk hard.
void buildY0U0V0Matrices(Mat3 &toY0U0V0, Mat3 &toRgb) {
  // The host hands the plugin a white balanced image, so the three channels
  // carry the same noise variance and the coefficients stay equal.
  const float mean = 3.0f * std::sqrt(3.0f);
  toY0U0V0.m[0][0] = mean;
  toY0U0V0.m[0][1] = mean;
  toY0U0V0.m[0][2] = mean;
  const float stddevU0 = std::sqrt(0.25f + 0.25f);
  const float stddevV0 = std::sqrt(0.25f * 0.25f + 0.5f * 0.5f + 0.25f * 0.25f);
  toY0U0V0.m[1][0] = 0.5f / stddevU0;
  toY0U0V0.m[1][1] = 0.0f;
  toY0U0V0.m[1][2] = -0.5f / stddevU0;
  toY0U0V0.m[2][0] = 0.25f / stddevV0;
  toY0U0V0.m[2][1] = -0.5f / stddevV0;
  toY0U0V0.m[2][2] = 0.25f / stddevV0;
  if (invert3x3(toY0U0V0, toRgb)) return;

  // darktable falls back to the plain mean when its own matrix turns out to be
  // singular. Both forms are invertible for any positive white balance, so this
  // only guards the constants above.
  const float fallbackY0 = 1.0f / (3.0f * std::sqrt(1.0f / 3.0f));
  toY0U0V0.m[0][0] = fallbackY0;
  toY0U0V0.m[0][1] = fallbackY0;
  toY0U0V0.m[0][2] = fallbackY0;
  invert3x3(toY0U0V0, toRgb);
}

// ---------------------------------------------------------------------------
// The noise model and the transform it drives
// ---------------------------------------------------------------------------

struct NoiseModel {
  // Generalized Anscombe parameters, from the green channel fit of the profile.
  float a = 1.0f;
  float b = 0.0f;
  float strength = 1.0f;
  float shadows = 1.0f;
  float bias = 0.0f;
  // Resolution the model describes relative to the full image, at most 1.
  float inScale = 1.0f;
  ColorMode mode = kY0U0V0Mode;

  // Derived when the model is initialized.
  float compensateP = 1.0f;  // keeps the shadow fulcrum fixed under `shadows`
  float p[3] = {1.0f, 1.0f, 1.0f};
  // darktable's white balance array, which is the strength here: the image
  // arrives white balanced, so only the strength is left to fold in.
  float signalScale[3] = {1.0f, 1.0f, 1.0f};
  float expon[3] = {2.0f, 2.0f, 2.0f};
  float amp = 1.0f;  // sqrt(a * compensateP)
  Mat3 forward;      // into the wavelet space
  Mat3 backward;     // back to RGB
};

void initNoiseModel(NoiseModel &model) {
  model.compensateP = kPFulcrum / std::pow(kPFulcrum, model.shadows);

  // darktable puts more strength into the Y0U0V0 path, otherwise that mode
  // smooths noticeably less than the per-channel one.
  float modeStrength = 1.0f;
  if (model.mode == kY0U0V0Mode) modeStrength = 2.5f;
  const float scale = model.strength * modeStrength;

  // The model describes the full sized frame. A frame the host hands over at a
  // proxy scale is an average of more than one sensor pixel per pixel, and an
  // average of 1/s^2 pixels carries 1/s^2 of the variance, so both parts of the
  // model follow the scale. darktable divides the signal by the scale instead,
  // which is only exact for the Poisson part; scaling the model keeps the
  // transformation of a given signal identical at every resolution, so a preview
  // is denoised exactly like the export it stands in for.
  const float varianceScale = model.inScale * model.inScale;
  model.a *= varianceScale;
  model.b *= varianceScale;

  for (int c = 0; c < 3; ++c) {
    model.p[c] = std::max(model.shadows, 0.0f);
    model.expon[c] = 1.0f / (1.0f - model.p[c] / 2.0f);
    model.signalScale[c] = scale;
  }
  model.amp = std::sqrt(model.a * model.compensateP);

  Mat3 toY0U0V0 = {{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
  Mat3 toRgb = toY0U0V0;
  if (model.mode == kY0U0V0Mode) {
    buildY0U0V0Matrices(toY0U0V0, toRgb);
    // Fold the strength into the matrices, so the wavelets see the stabilized
    // channels already scaled by it.
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) {
        toY0U0V0.m[r][c] /= scale;
        toRgb.m[r][c] *= scale;
      }
  }
  model.forward = toY0U0V0;
  model.backward = toRgb;
}

// The frame the model describes, relative to the project. A host renders a
// preview and a draft pass at a fraction of the project resolution, and noise
// averages down with the pixel size, so the transform has to follow the frame it
// is handed. The render scale is 1 whenever the frame is full sized.
float renderScale(const OFX::RenderArguments &args) {
  const double scale = std::max(args.renderScale.x, args.renderScale.y);
  if (!(scale > 0.0)) return 1.0f;
  if (scale > 1.0) return 1.0f;
  return (float)scale;
}

// Generalized Anscombe transform: maps a signal whose noise follows a * X + b
// onto one with unit variance. The exponent p generalizes the classic transform
// so that the shadows can be compressed or stretched independently.
void preconditionPixel(const NoiseModel &model, const float linear[3], float out[3]) {
  float stabilized[3];
  // The Y0U0V0 path divides by the strength through its matrix, so only the
  // per-channel path divides the signal here.
  for (int c = 0; c < 3; ++c) {
    float v = linear[c];
    if (model.mode == kRgbMode) v /= model.signalScale[c];
    v = std::max(v + model.b, 0.0f);
    stabilized[c] = std::pow(v, 1.0f - model.p[c] / 2.0f) * 2.0f / ((2.0f - model.p[c]) * model.amp);
  }
  matVec(model.forward, stabilized, out);
}

// Inverse of the transform, using the low-bias closed form darktable fits to the
// statistics of the forward transform. `value` is a denoised stabilized pixel.
void backtransformPixel(const NoiseModel &model, const float value[3], float linear[3]) {
  const float bias = model.bias;
  float rgb[3];
  // The identity in the per-channel mode.
  matVec(model.backward, value, rgb);
  for (int c = 0; c < 3; ++c) {
    const float x = std::max(rgb[c], 0.0f);
    float channelBias = bias;
    if (model.mode != kRgbMode) channelBias = bias * model.signalScale[c];
    const float delta = x * x + channelBias;
    const float z = (x + std::sqrt(std::max(delta, 0.0f))) * model.amp * (2.0f - model.p[c]) / 4.0f;
    const float back = std::pow(z, model.expon[c]);
    if (model.mode == kRgbMode) linear[c] = model.signalScale[c] * (back - model.b);
    else linear[c] = back - model.b;
  }
}

// ---------------------------------------------------------------------------
// Wavelet bands
// ---------------------------------------------------------------------------

// Variance of a unit-variance signal after one scale of the a-trous filter.
float bandNoiseFactor(int scale) {
  const float varf = std::sqrt(2.0f + 2.0f * 4.0f * 4.0f + 6.0f * 6.0f) / 16.0f;
  return std::pow(varf, (float)scale);
}

// Color distance weight of one filter tap. Neighbours whose color is far from
// the center pixel are pushed toward zero, which keeps the low pass from
// averaging across an edge. The distance is measured in standard deviations of
// the stabilized noise, three of which are the point where the weight starts to
// fall off.
float tapWeight(const float *center, const float *neighbour, float invSigma2) {
  float distance2 = 0.0f;
  for (int c = 0; c < 3; ++c) {
    const float d = center[c] - neighbour[c];
    distance2 += d * d;
  }
  return std::exp2(-std::max(0.0f, distance2 * invSigma2 * 0.02f - 9.0f));
}

// Per-worker accumulator for the detail energy of one band.
struct BandEnergy {
  double sum[3] = {0.0, 0.0, 0.0};
};

// Runs body(begin, end, acc) over the rows of a frame, one acc per worker. Small
// frames stay on a single thread so that the handoff does not show up in the
// render time.
constexpr int kMaxWorkers = 32;

template <typename Body>
std::vector<BandEnergy> parallelRows(int rows, Body body) {
  unsigned count = std::thread::hardware_concurrency();
  if (count == 0) count = 1;
  int workers = (int)std::min<unsigned>(count, kMaxWorkers);
  if (rows / 8 < workers) workers = std::max(1, rows / 8);
  std::vector<BandEnergy> energy((size_t)workers);
  if (workers == 1) {
    body(0, rows, energy[0]);
    return energy;
  }
  const int chunk = (rows + workers - 1) / workers;
  std::vector<std::thread> pool;
  pool.reserve((size_t)workers - 1);
  int begin = std::min(rows, chunk);
  for (int w = 1; w < workers && begin < rows; ++w) {
    const int end = std::min(rows, begin + chunk);
    BandEnergy *acc = &energy[(size_t)w];
    pool.emplace_back([&body, acc, begin, end] { body(begin, end, *acc); });
    begin = end;
  }
  body(0, std::min(rows, chunk), energy[0]);
  for (std::thread &worker : pool) worker.join();
  return energy;
}

// One a-trous band: an edge-aware B3 spline low pass at the stride of the band,
// plus the detail it did not capture. `energy` collects the squared detail, which
// is what sizes the threshold of the band.
void decomposeRows(const float *in, float *coarse, float *detail, int scale, float invSigma2, int width, int height,
                   int rowBegin, int rowEnd, BandEnergy &energy) {
  const int mult = 1 << scale;
  for (int y = rowBegin; y < rowEnd; ++y) {
    const float *inRow = in + 4 * (size_t)y * width;
    float *coarseRow = coarse + 4 * (size_t)y * width;
    float *detailRow = detail + 4 * (size_t)y * width;
    for (int x = 0; x < width; ++x) {
      const float *center = inRow + 4 * x;
      float sum[3] = {0.0f, 0.0f, 0.0f};
      float weight[3] = {0.0f, 0.0f, 0.0f};
      int tap = 0;
      for (int jj = 0; jj < 5; ++jj) {
        const int sy = std::clamp(y + mult * (jj - 2), 0, height - 1);
        const float *neighbourRow = in + 4 * (size_t)sy * width;
        for (int ii = 0; ii < 5; ++ii) {
          const int sx = std::clamp(x + mult * (ii - 2), 0, width - 1);
          const float *neighbour = neighbourRow + 4 * sx;
          const float tapWeighted = kFilter[tap++] * tapWeight(center, neighbour, invSigma2);
          for (int c = 0; c < 3; ++c) {
            weight[c] += tapWeighted;
            sum[c] += tapWeighted * neighbour[c];
          }
        }
      }
      for (int c = 0; c < 3; ++c) {
        sum[c] /= weight[c];
        const float d = center[c] - sum[c];
        energy.sum[c] += (double)d * d;
        coarseRow[4 * x + c] = sum[c];
        detailRow[4 * x + c] = d;
      }
    }
  }
}

// BayesShrink threshold of one band: the noise variance the band would carry if
// the transform had left unit variance, over the standard deviation of the
// signal that is left once that noise is accounted for.
void bandThreshold(float threshold[3], const std::vector<BandEnergy> &energy, size_t npixels, int scale) {
  const float sigmaBand = bandNoiseFactor(scale);
  const float sb2 = sigmaBand * sigmaBand;

  // darktable's frequency curve (a force of 0.5 per band) and the factor of 8
  // that keeps the shrinkage from eating the texture along with the noise.
  const float adjt = 8.0f * (kBandForce * kBandForce * 4.0f);

  double sumSquared[3] = {0.0, 0.0, 0.0};
  for (const BandEnergy &part : energy)
    for (int c = 0; c < 3; ++c) sumSquared[c] += part.sum[c];

  for (int c = 0; c < 3; ++c) {
    const float variance = (float)(sumSquared[c] / (double)(npixels - 1));
    const float signalStd = std::sqrt(std::max(1e-6f, variance - sb2));
    threshold[c] = adjt * sb2 / signalStd;
  }
}

// Adds one band of thresholded detail to the reconstruction: detail below the
// threshold is dropped, the rest is pulled toward zero by the threshold.
void synthesizeRow(float *out, const float *detail, const float threshold[3], int width) {
  for (int x = 0; x < width; ++x) {
    for (int c = 0; c < 3; ++c) {
      const float d = detail[4 * x + c];
      out[4 * x + c] += std::max(d - threshold[c], 0.0f) + std::min(d + threshold[c], 0.0f);
    }
  }
}

// Number of bands to run: darktable stops adding bands once the filter is wider
// than a fifth of the frame. The count depends on the frame, not on the scale it
// is rendered at, so a proxy pass shrinks the same frequencies a full sized pass
// would.
int bandCount(int width, int height) {
  const int longest = std::max(width, height);
  const float widest = (float)(2 * (2 << (kBands - 1)) + 1);
  const float support = std::min(widest, (float)longest * 0.2f);
  const float i0 = std::log2((support - 1.0f) * 0.5f);
  if (!(i0 > 0.0f)) return 0;
  int count = 0;
  for (; count < kBands; ++count) {
    const float supportIn = (float)(2 * (2 << count) + 1);
    const float iIn = std::log2((supportIn - 1.0f) * 0.5f) - 1.0f;
    const float t = 1.0f - (iIn + 0.5f) / i0;
    if (t < 0.0f) break;
  }
  return count;
}

class DenoisePlugin : public OFX::ImageEffect {
public:
  explicit DenoisePlugin(OfxImageEffectHandle handle)
      : ImageEffect(handle) {
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    inputSpace_ = fetchChoiceParam("inputSpace");
    noiseSlope_ = fetchDoubleParam("noiseSlope");
    noiseOffset_ = fetchDoubleParam("noiseOffset");
    strength_ = fetchDoubleParam("strength");
    shadows_ = fetchDoubleParam("shadows");
    bias_ = fetchDoubleParam("bias");
    colorMode_ = fetchChoiceParam("colorMode");
  }

  void render(const OFX::RenderArguments &args) override;

private:
  void readModel(const OFX::RenderArguments &args, NoiseModel &model, bool &srgbCurve);

  OFX::Clip *dstClip_;
  OFX::Clip *srcClip_;
  OFX::ChoiceParam *inputSpace_;
  OFX::DoubleParam *noiseSlope_;
  OFX::DoubleParam *noiseOffset_;
  OFX::DoubleParam *strength_;
  OFX::DoubleParam *shadows_;
  OFX::DoubleParam *bias_;
  OFX::ChoiceParam *colorMode_;
};

void DenoisePlugin::readModel(const OFX::RenderArguments &args, NoiseModel &model, bool &srgbCurve) {
  int space = kLinearRec709;
  inputSpace_->getValueAtTime(args.time, space);
  if (space < kSrgb || space > kLinearRec2020) space = kLinearRec709;
  srgbCurve = spaceUsesSrgbCurve(space);

  int mode = kY0U0V0Mode;
  colorMode_->getValueAtTime(args.time, mode);
  if (mode != kRgbMode && mode != kY0U0V0Mode) mode = kY0U0V0Mode;
  model.mode = (ColorMode)mode;

  model.a = (float)std::max(noiseSlope_->getValueAtTime(args.time), 1e-12);
  model.b = (float)std::max(noiseOffset_->getValueAtTime(args.time), 0.0);
  model.strength = (float)std::max(strength_->getValueAtTime(args.time), 1e-3);
  model.shadows = (float)shadows_->getValueAtTime(args.time);
  model.bias = (float)bias_->getValueAtTime(args.time);
  model.inScale = renderScale(args);
  initNoiseModel(model);
}

void DenoisePlugin::render(const OFX::RenderArguments &args) {
  std::unique_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
  std::unique_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
  if (!dst || !src) return;

  if (dst->getPixelDepth() != OFX::eBitDepthFloat || src->getPixelDepth() != OFX::eBitDepthFloat ||
      dst->getPixelComponents() != OFX::ePixelComponentRGBA || src->getPixelComponents() != OFX::ePixelComponentRGBA)
    OFX::throwSuiteStatusException(kOfxStatErrUnsupported);

  const OfxRectI &sb = src->getBounds();
  const OfxRectI &db = dst->getBounds();
  const int width = sb.x2 - sb.x1;
  const int height = sb.y2 - sb.y1;
  if (width <= 0 || height <= 0) return;
  // The statistics of every band cover the whole frame, so the plugin needs the
  // whole frame in one call. describe() reports that it cannot be tiled.
  if (db.x2 - db.x1 < width || db.y2 - db.y1 < height) OFX::throwSuiteStatusException(kOfxStatErrFormat);

  NoiseModel model;
  bool srgbCurve = false;
  readModel(args, model, srgbCurve);

  const size_t npixels = (size_t)width * (size_t)height;
  const int bands = bandCount(width, height);
  int largestStride = 1;
  if (bands > 0) largestStride = 1 << (bands - 1);

  // A frame too small for even one band is passed through untouched.
  if (bands == 0 || width < 2 * largestStride || height < 2 * largestStride) {
    for (int y = 0; y < height; ++y) {
      const float *s = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + y));
      float *d = static_cast<float *>(dst->getPixelAddress(db.x1, db.y1 + y));
      if (!s || !d) continue;
      std::copy(s, s + (size_t)4 * width, d);
    }
    return;
  }

  // Three scratch frames: the stabilized copy, the coarse band that ping-pongs
  // with it, and the detail of the band being thresholded. The output clip
  // carries the reconstruction.
  std::vector<float> stabilized(4 * npixels);
  std::vector<float> coarse(4 * npixels);
  std::vector<float> detail(4 * npixels);
  float *coarseBuf = coarse.data();
  float *pingBuf = stabilized.data();

  // Variance stabilizing transform, reading the source in linear light, and a
  // cleared output to accumulate the reconstruction in.
  parallelRows(height, [&](int begin, int end, BandEnergy &) {
    for (int y = begin; y < end; ++y) {
      const float *s = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + y));
      float *p = pingBuf + 4 * (size_t)y * width;
      float *d = static_cast<float *>(dst->getPixelAddress(db.x1, db.y1 + y));
      if (d) std::fill(d, d + (size_t)4 * width, 0.0f);
      if (!s || !d) {
        std::fill(p, p + (size_t)4 * width, 0.0f);
        continue;
      }
      for (int x = 0; x < width; ++x) {
        float linear[3];
        for (int c = 0; c < 3; ++c) linear[c] = decodeToLinear(s[4 * x + c], srgbCurve);
        float transformed[3];
        preconditionPixel(model, linear, transformed);
        for (int c = 0; c < 3; ++c) p[4 * x + c] = transformed[c];
      }
    }
  });

  for (int scale = 0; scale < bands; ++scale) {
    if (abort()) return;
    const float sigmaBand = bandNoiseFactor(scale);
    const float invSigma2 = 1.0f / (sigmaBand * sigmaBand);

    std::vector<BandEnergy> energy = parallelRows(height, [&](int begin, int end, BandEnergy &acc) {
      decomposeRows(pingBuf, coarseBuf, detail.data(), scale, invSigma2, width, height, begin, end, acc);
    });

    float threshold[3];
    bandThreshold(threshold, energy, npixels, scale);

    parallelRows(height, [&](int begin, int end, BandEnergy &) {
      for (int y = begin; y < end; ++y) {
        float *d = static_cast<float *>(dst->getPixelAddress(db.x1, db.y1 + y));
        if (!d) continue;
        synthesizeRow(d, detail.data() + 4 * (size_t)y * width, threshold, width);
      }
    });

    std::swap(coarseBuf, pingBuf);
  }

  // `pingBuf` now holds the residue. Add it, invert the transform, and hand the
  // result back in the transfer curve the source came in with.
  parallelRows(height, [&](int begin, int end, BandEnergy &) {
    for (int y = begin; y < end; ++y) {
      const float *s = static_cast<const float *>(src->getPixelAddress(sb.x1, sb.y1 + y));
      const float *residue = pingBuf + 4 * (size_t)y * width;
      float *d = static_cast<float *>(dst->getPixelAddress(db.x1, db.y1 + y));
      if (!d) continue;
      for (int x = 0; x < width; ++x) {
        float transformed[3];
        for (int c = 0; c < 3; ++c) transformed[c] = d[4 * x + c] + residue[4 * x + c];
        float linear[3];
        backtransformPixel(model, transformed, linear);
        for (int c = 0; c < 3; ++c) {
          // The inverse transform undershoots slightly around edges, and light
          // is not negative.
          const float value = std::max(linear[c], 0.0f);
          d[4 * x + c] = encodeFromLinear(value, srgbCurve);
        }
        if (s) d[4 * x + 3] = s[4 * x + 3];
      }
    }
  });
}

}  // namespace

mDeclarePluginFactory(DenoisePluginFactory, {}, {});

void DenoisePluginFactory::describe(OFX::ImageEffectDescriptor &desc) {
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

void DenoisePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum) {
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

  DoubleParamDescriptor *noiseSlope = desc.defineDoubleParam("noiseSlope");
  noiseSlope->setLabels("Noise Slope (a)", "Slope", "Poisson part of the sensor noise model V(X) = a * X + b.");
  noiseSlope->setHint("Poisson part of the sensor noise model V(X) = a * X + b, where X is the linear signal with 1.0 at the sensor white level. The host fills this in from the camera and ISO it reads out of a RAW file, using the bundled noise profile database; the default is a typical full-frame value for ISO 1600 and applies to a file that carries no camera data. Raise it for a noisier frame, lower it for a cleaner one.");
  noiseSlope->setDefault(3.55e-05);
  noiseSlope->setRange(0.0, 0.02);
  noiseSlope->setDisplayRange(0.0, 1.0e-3);
  noiseSlope->setIncrement(1e-7);
  page->addChild(*noiseSlope);

  DoubleParamDescriptor *noiseOffset = desc.defineDoubleParam("noiseOffset");
  noiseOffset->setLabels("Noise Offset (b)", "Offset", "Gaussian part of the sensor noise model V(X) = a * X + b.");
  noiseOffset->setHint("Gaussian part of the sensor noise model V(X) = a * X + b: the read noise that is there even with no light, which is what dominates the deep shadows. The host fills this in from the camera and ISO it reads out of a RAW file, using the bundled noise profile database; the default is a typical full-frame value for ISO 1600 and applies to a file that carries no camera data.");
  noiseOffset->setDefault(1.07e-07);
  noiseOffset->setRange(0.0, 2.0e-05);
  noiseOffset->setDisplayRange(0.0, 5.0e-6);
  noiseOffset->setIncrement(1e-9);
  page->addChild(*noiseOffset);

  DoubleParamDescriptor *strength = desc.defineDoubleParam("strength");
  strength->setLabels("Strength", "Strength", "How hard the wavelets shrink the detail.");
  strength->setHint("Finetune the denoising strength. The default of 1 resolves the noise the model above describes; raise it to remove more of it at the cost of fine texture, lower it to leave some of the noise in place.");
  strength->setDefault(1.0);
  strength->setRange(0.001, 1000.0);
  strength->setDisplayRange(0.1, 4.0);
  strength->setIncrement(0.001);
  page->addChild(*strength);

  DoubleParamDescriptor *shadows = desc.defineDoubleParam("shadows");
  shadows->setLabels("Preserve Shadows", "Shadows", "Finetune the denoising of the dark areas.");
  shadows->setHint("Finetune the denoising of the dark areas. Decrease it to denoise them more aggressively, increase it to leave more shadow detail at the cost of shadow noise. The default is 1.");
  shadows->setDefault(1.0);
  shadows->setRange(0.0, 1.8);
  shadows->setDisplayRange(0.0, 1.8);
  shadows->setIncrement(0.01);
  page->addChild(*shadows);

  DoubleParamDescriptor *bias = desc.defineDoubleParam("bias");
  bias->setLabels("Bias Correction", "Bias", "Corrects a color cast the inverse transform leaves in the shadows.");
  bias->setHint("Corrects a color cast in the shadows that the inverse transform can leave behind. Decrease it if the shadows are too purple, increase it if they are too green. The default of 0 suits most frames.");
  bias->setDefault(0.0);
  bias->setRange(-1000.0, 100.0);
  bias->setDisplayRange(-10.0, 10.0);
  bias->setIncrement(0.01);
  page->addChild(*bias);

  ChoiceParamDescriptor *colorMode = desc.defineChoiceParam("colorMode");
  colorMode->setLabels("Color Mode", "Color", "Color representation the wavelets run in.");
  colorMode->setHint("Color representation the wavelets run in. Y0U0V0 sums the channels into a luma-like signal that it denoises on its own, and denoises the color differences separately, which keeps chroma noise down without flattening the fine texture. RGB keeps the channels apart.");
  colorMode->setDefault(kY0U0V0Mode);
  colorMode->appendOption("RGB");
  colorMode->appendOption("Y0U0V0");
  page->addChild(*colorMode);

  ChoiceParamDescriptor *inputSpace = desc.defineChoiceParam("inputSpace");
  inputSpace->setLabels("Input Color Space", "Input", "Color space of the source pixels.");
  inputSpace->setHint("Color space of the source pixels. The host sets this from the opened image. The transform needs linear light, so the transfer curve is removed on the way in and applied again on the way out, and the noise model is read in that linear space.");
  inputSpace->setDefault(kLinearRec709);
  inputSpace->appendOption("sRGB");
  inputSpace->appendOption("Display P3");
  inputSpace->appendOption("Linear Rec.709");
  inputSpace->appendOption("Linear Rec.2020");
  page->addChild(*inputSpace);
}

OFX::ImageEffect *DenoisePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum) {
  return new DenoisePlugin(handle);
}

void registerDenoisePlugin(OFX::PluginFactoryArray &ids) {
  static DenoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
  ids.push_back(&p);
}
