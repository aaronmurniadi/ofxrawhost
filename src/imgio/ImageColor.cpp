#include "imgio/ImageIO.h"
#include "imgio/ImageIOPriv.h"
#include "perf.h"

#include <lcms2.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

const char *colorSpaceName(ColorSpace cs) {
  switch (cs) {
    case ColorSpace::sRGB: return "sRGB";
    case ColorSpace::DisplayP3: return "Display P3";
    case ColorSpace::LinearRec709: return "Linear Rec.709";
    case ColorSpace::LinearRec2020: return "Linear Rec.2020";
  }
  return "sRGB";
}

static cmsToneCurve *srgbCurve() {
  // Same parametric curve cmsCreate_sRGBProfile uses.
  cmsFloat64Number params[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
  return cmsBuildParametricToneCurve(nullptr, 4, params);
}

static cmsHPROFILE makeProfile(ColorSpace cs) {
  const cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
  switch (cs) {
    case ColorSpace::sRGB:
      return cmsCreate_sRGBProfile();
    case ColorSpace::DisplayP3: {
      cmsCIExyYTRIPLE p3 = {{0.680, 0.320, 1.0}, {0.265, 0.690, 1.0}, {0.150, 0.060, 1.0}};
      cmsToneCurve *trc = srgbCurve();
      cmsToneCurve *curves[3] = {trc, trc, trc};
      cmsHPROFILE p = cmsCreateRGBProfile(&d65, &p3, curves);
      cmsFreeToneCurve(trc);
      return p;
    }
    case ColorSpace::LinearRec709: {
      cmsCIExyYTRIPLE r709 = {{0.640, 0.330, 1.0}, {0.300, 0.600, 1.0}, {0.150, 0.060, 1.0}};
      cmsToneCurve *lin = cmsBuildGamma(nullptr, 1.0);
      cmsToneCurve *curves[3] = {lin, lin, lin};
      cmsHPROFILE p = cmsCreateRGBProfile(&d65, &r709, curves);
      cmsFreeToneCurve(lin);
      return p;
    }
    case ColorSpace::LinearRec2020: {
      cmsCIExyYTRIPLE r2020 = {{0.708, 0.292, 1.0}, {0.170, 0.797, 1.0}, {0.131, 0.046, 1.0}};
      cmsToneCurve *lin = cmsBuildGamma(nullptr, 1.0);
      cmsToneCurve *curves[3] = {lin, lin, lin};
      cmsHPROFILE p = cmsCreateRGBProfile(&d65, &r2020, curves);
      cmsFreeToneCurve(lin);
      return p;
    }
  }
  return cmsCreate_sRGBProfile();
}

// Cache for deterministic ICC profiles, serialized bytes, and CMS transforms.
// Profiles are recreated from scratch on every call without this cache, which is
// expensive (lcms2 profile building + CMS transform linking) and called per-frame.
static cmsHPROFILE cachedProfile(ColorSpace cs) {
  static std::array<cmsHPROFILE, 4> profiles{};
  const int idx = (int)cs;
  if (!profiles[idx]) profiles[idx] = makeProfile(cs);
  return profiles[idx];
}

static cmsHPROFILE srgbProfile() {
  static cmsHPROFILE p = cmsCreate_sRGBProfile();
  return p;
}

static const std::vector<uint8_t> &cachedIccBytes(ColorSpace cs) {
  static std::array<std::vector<uint8_t>, 4> bytes{};
  static std::array<bool, 4> tried{};
  const int idx = (int)cs;
  if (tried[idx]) return bytes[idx];
  tried[idx] = true;
  cmsHPROFILE p = cachedProfile(cs);
  if (!p) return bytes[idx];
  cmsUInt32Number n = 0;
  if (cmsSaveProfileToMem(p, nullptr, &n) && n > 0) {
    bytes[idx].resize(n);
    if (cmsSaveProfileToMem(p, bytes[idx].data(), &n))
      bytes[idx].resize(n);
    else
      bytes[idx].clear();
  }
  return bytes[idx];
}

static cmsHTRANSFORM cachedTransform(ColorSpace cs) {
  static std::array<cmsHTRANSFORM, 4> transforms{};
  static std::array<bool, 4> tried{};
  const int idx = (int)cs;
  if (tried[idx]) return transforms[idx];
  tried[idx] = true;
  cmsHPROFILE src = cachedProfile(cs);
  cmsHPROFILE dst = srgbProfile();
  if (src && dst)
    transforms[idx] = cmsCreateTransform(src, TYPE_RGBA_FLT, dst, TYPE_RGBA_8, INTENT_RELATIVE_COLORIMETRIC,
                                         cmsFLAGS_NOCACHE | cmsFLAGS_COPY_ALPHA);
  return transforms[idx];
}

bool profileBytes(ColorSpace cs, std::vector<uint8_t> &out) {
  const auto &icc = cachedIccBytes(cs);
  if (icc.empty()) return false;
  out = icc;
  return true;
}

static std::string lowerCopy(const char *s) {
  std::string o;
  if (!s) return o;
  for (; *s; ++s) o.push_back((char)tolower((unsigned char)*s));
  return o;
}

bool extractPngIcc(const std::string &path, std::vector<uint8_t> &icc) {
  icc.clear();
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  uint8_t sig[8];
  if (fread(sig, 1, 8, f) != 8 || std::memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) {
    fclose(f);
    return false;
  }
  bool ok = false;
  for (;;) {
    uint8_t lenb[4], type[4];
    if (fread(lenb, 1, 4, f) != 4 || fread(type, 1, 4, f) != 4) break;
    const uint32_t len = ((uint32_t)lenb[0] << 24) | ((uint32_t)lenb[1] << 16) | ((uint32_t)lenb[2] << 8) | lenb[3];
    if (std::memcmp(type, "IEND", 4) == 0) break;
    if (std::memcmp(type, "iCCP", 4) == 0 && len > 2 && len < 64u * 1024u * 1024u) {
      std::vector<uint8_t> chunk(len);
      if (fread(chunk.data(), 1, len, f) != len) break;
      fseek(f, 4, SEEK_CUR);  // CRC
      size_t i = 0;
      while (i < chunk.size() && chunk[i]) ++i;
      if (i + 2 >= chunk.size() || chunk[i + 1] != 0) break;
      const uint8_t *comp = chunk.data() + i + 2;
      const uLong compLen = (uLong)(chunk.size() - (i + 2));
      uLongf destLen = compLen * 4 + 65536;
      for (int attempt = 0; attempt < 8; ++attempt) {
        icc.resize(destLen);
        const int z = uncompress(icc.data(), &destLen, comp, compLen);
        if (z == Z_OK) {
          icc.resize(destLen);
          ok = !icc.empty();
          break;
        }
        if (z != Z_BUF_ERROR) {
          icc.clear();
          break;
        }
        destLen *= 2;
      }
      break;
    }
    if (fseek(f, (long)len + 4, SEEK_CUR) != 0) break;
  }
  fclose(f);
  if (!ok) icc.clear();
  return ok;
}

bool extractJpgIcc(const std::string &path, std::vector<uint8_t> &icc) {
  icc.clear();
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  const long sz = ftell(f);
  if (sz < 4 || sz > 256L * 1024L * 1024L) {
    fclose(f);
    return false;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }
  std::vector<uint8_t> data((size_t)sz);
  if (fread(data.data(), 1, data.size(), f) != data.size()) {
    fclose(f);
    return false;
  }
  fclose(f);
  if (data[0] != 0xff || data[1] != 0xd8) return false;

  std::vector<std::vector<uint8_t>> parts;
  int expected = -1;
  size_t i = 2;
  while (i + 4 <= data.size()) {
    if (data[i] != 0xff) {
      ++i;
      continue;
    }
    while (i < data.size() && data[i] == 0xff) ++i;
    if (i >= data.size()) break;
    const uint8_t marker = data[i++];
    if (marker == 0xd9 || marker == 0xda) break;  // EOI / SOS
    if (marker >= 0xd0 && marker <= 0xd7) continue;  // RSTn
    if (i + 2 > data.size()) break;
    const uint16_t seglen = (uint16_t)((data[i] << 8) | data[i + 1]);
    if (seglen < 2 || i + seglen > data.size()) break;
    if (marker == 0xe2 && seglen >= 16) {
      const uint8_t *p = data.data() + i + 2;
      if (std::memcmp(p, "ICC_PROFILE\0", 12) == 0) {
        const int seq = p[12], cnt = p[13];
        if (seq >= 1 && cnt >= 1) {
          if (expected < 0) {
            expected = cnt;
            parts.assign((size_t)cnt, {});
          }
          if (cnt == expected && seq <= expected)
            parts[(size_t)seq - 1].assign(p + 14, p + seglen - 2);
        }
      }
    }
    i += seglen;
  }
  if (expected <= 0) return false;
  size_t total = 0;
  for (const auto &p : parts) {
    if (p.empty()) return false;
    total += p.size();
  }
  icc.reserve(total);
  for (const auto &p : parts) icc.insert(icc.end(), p.begin(), p.end());
  return !icc.empty();
}

static bool profilePrimaries(cmsHPROFILE p, cmsCIExyYTRIPLE &prim, cmsCIExyY &wp) {
  const cmsCIEXYZ *w = (const cmsCIEXYZ *)cmsReadTag(p, cmsSigMediaWhitePointTag);
  const cmsCIEXYZ *r = (const cmsCIEXYZ *)cmsReadTag(p, cmsSigRedColorantTag);
  const cmsCIEXYZ *g = (const cmsCIEXYZ *)cmsReadTag(p, cmsSigGreenColorantTag);
  const cmsCIEXYZ *b = (const cmsCIEXYZ *)cmsReadTag(p, cmsSigBlueColorantTag);
  if (!w || !r || !g || !b) return false;
  cmsXYZ2xyY(&wp, w);
  cmsXYZ2xyY(&prim.Red, r);
  cmsXYZ2xyY(&prim.Green, g);
  cmsXYZ2xyY(&prim.Blue, b);
  return true;
}

static bool profileLooksLinear(cmsHPROFILE p) {
  const cmsToneCurve *trc = (const cmsToneCurve *)cmsReadTag(p, cmsSigRedTRCTag);
  if (!trc) return false;
  const cmsFloat32Number out = cmsEvalToneCurveFloat((cmsToneCurve *)trc, 0.5f);
  return std::fabs((double)out - 0.5) < 0.05;
}

static double primDist2(const cmsCIExyYTRIPLE &a, const cmsCIExyYTRIPLE &b) {
  const auto d = [](const cmsCIExyY &x, const cmsCIExyY &y) {
    const double dx = x.x - y.x, dy = x.y - y.y;
    return dx * dx + dy * dy;
  };
  return d(a.Red, b.Red) + d(a.Green, b.Green) + d(a.Blue, b.Blue);
}

ColorSpace classifyIcc(const std::vector<uint8_t> &icc) {
  if (icc.empty()) return ColorSpace::sRGB;
  cmsHPROFILE p = cmsOpenProfileFromMem(icc.data(), (cmsUInt32Number)icc.size());
  if (!p) return ColorSpace::sRGB;

  char desc[256] = {};
  cmsGetProfileInfoASCII(p, cmsInfoDescription, "en", "US", desc, sizeof desc);
  const std::string d = lowerCopy(desc);
  ColorSpace fromDesc = ColorSpace::sRGB;
  bool haveDesc = false;
  if (d.find("prophoto") != std::string::npos || d.find("rec2020") != std::string::npos ||
      d.find("rec-2020") != std::string::npos || d.find("rec.2020") != std::string::npos ||
      d.find("bt.2020") != std::string::npos || d.find("bt2020") != std::string::npos) {
    fromDesc = ColorSpace::LinearRec2020;
    haveDesc = true;
  } else if (d.find("display p3") != std::string::npos || d.find("display-p3") != std::string::npos ||
             (d.find("p3") != std::string::npos && d.find("dci") == std::string::npos)) {
    fromDesc = ColorSpace::DisplayP3;
    haveDesc = true;
  } else if (d.find("rec709") != std::string::npos || d.find("rec-709") != std::string::npos ||
             d.find("rec.709") != std::string::npos || d.find("bt.709") != std::string::npos ||
             d.find("bt709") != std::string::npos) {
    fromDesc = profileLooksLinear(p) ? ColorSpace::LinearRec709 : ColorSpace::sRGB;
    haveDesc = true;
  } else if (d.find("srgb") != std::string::npos) {
    fromDesc = ColorSpace::sRGB;
    haveDesc = true;
  }
  if (haveDesc) {
    // Wide-gamut linear names (ProPhoto) already mapped to Rec.2020.
    if (fromDesc == ColorSpace::DisplayP3 && profileLooksLinear(p)) {
      // Linear P3 is rare; keep Display P3 tag (plugin list has no Linear P3).
    }
    cmsCloseProfile(p);
    return fromDesc;
  }

  cmsCIExyYTRIPLE prim{};
  cmsCIExyY wp{};
  if (!profilePrimaries(p, prim, wp)) {
    cmsCloseProfile(p);
    return ColorSpace::sRGB;
  }
  const bool linear = profileLooksLinear(p);
  const cmsCIExyYTRIPLE known[4] = {
      {{0.640, 0.330, 1.0}, {0.300, 0.600, 1.0}, {0.150, 0.060, 1.0}},  // sRGB / 709
      {{0.680, 0.320, 1.0}, {0.265, 0.690, 1.0}, {0.150, 0.060, 1.0}},  // P3
      {{0.640, 0.330, 1.0}, {0.300, 0.600, 1.0}, {0.150, 0.060, 1.0}},  // Linear Rec.709
      {{0.708, 0.292, 1.0}, {0.170, 0.797, 1.0}, {0.131, 0.046, 1.0}},  // Linear Rec.2020
  };
  const ColorSpace spaces[4] = {ColorSpace::sRGB, ColorSpace::DisplayP3, ColorSpace::LinearRec709,
                                ColorSpace::LinearRec2020};
  double best = 1e9;
  ColorSpace pick = ColorSpace::sRGB;
  for (int i = 0; i < 4; ++i) {
    // Skip gamma spaces when TRC is linear, and linear spaces when TRC is not.
    if (linear && (spaces[i] == ColorSpace::sRGB || spaces[i] == ColorSpace::DisplayP3)) continue;
    if (!linear && (spaces[i] == ColorSpace::LinearRec709 || spaces[i] == ColorSpace::LinearRec2020)) continue;
    const double dist = primDist2(prim, known[i]);
    if (dist < best) {
      best = dist;
      pick = spaces[i];
    }
  }
  // If filters removed every candidate, fall back to unconstrained nearest.
  if (best >= 1e9) {
    for (int i = 0; i < 4; ++i) {
      const double dist = primDist2(prim, known[i]);
      if (dist < best) {
        best = dist;
        pick = spaces[i];
      }
    }
    if (linear && pick == ColorSpace::sRGB) pick = ColorSpace::LinearRec709;
    if (linear && pick == ColorSpace::DisplayP3) pick = ColorSpace::LinearRec2020;
  }
  cmsCloseProfile(p);
  return pick;
}

void toDisplayRGBA8(const Image &img, ColorSpace space, std::vector<unsigned char> &out) {
  PerfScope _ps("toDisplayRGBA8");
  out.assign((size_t)img.w * img.h * 4, 0);
  if (img.w <= 0 || img.h <= 0) return;

  cmsHTRANSFORM xform = cachedTransform(space);
  const int rowFloats = img.w * 4;
  if (xform) {
    for (int y = 0; y < img.h; ++y) {
      const float *src = img.px.data() + (size_t)(img.h - 1 - y) * rowFloats;
      unsigned char *dst = out.data() + (size_t)y * rowFloats;
      cmsDoTransform(xform, src, dst, (cmsUInt32Number)img.w);
    }
  } else {
    for (int y = 0; y < img.h; ++y) {
      const float *src = img.px.data() + (size_t)(img.h - 1 - y) * rowFloats;
      unsigned char *dst = out.data() + (size_t)y * rowFloats;
      for (int i = 0; i < rowFloats; ++i)
        dst[i] = (unsigned char)std::lround(std::clamp(src[i], 0.0f, 1.0f) * 255.0f);
    }
  }
}
