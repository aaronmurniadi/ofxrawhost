#include "ImageIO.h"

#include <libraw/libraw.h>
#include <lcms2.h>
#include <tiffio.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__F16C__)
#include <immintrin.h>
#endif

// Enable NEON paths on ARM for stb_image (auto-enabled for x86_64 SSE2).
#if defined(__ARM_NEON) && !defined(STBI_NEON)
#define STBI_NEON
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"  // stb HDR path uses sprintf
#endif
#include "stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "tinyexr.h"

namespace fs = std::filesystem;

static std::mutex gLibRawDecodeMutex;

const char *colorSpaceName(ColorSpace cs) {
  switch (cs) {
    case ColorSpace::sRGB: return "sRGB";
    case ColorSpace::DisplayP3: return "Display P3";
    case ColorSpace::LinearRec709: return "Linear Rec.709";
    case ColorSpace::LinearRec2020: return "Linear Rec.2020";
  }
  return "sRGB";
}

static void flipRows(float *px, int w, int h) {
  const size_t row = (size_t)w * 4;
  for (int y = 0; y < h / 2; ++y)
    std::swap_ranges(px + (size_t)y * row, px + (size_t)(y + 1) * row, px + (size_t)(h - 1 - y) * row);
}

static bool fromRGBAFloatTopDown(float *src, int w, int h, Image &out) {
  out.w = w;
  out.h = h;
  out.px.assign(src, src + (size_t)w * h * 4);
  flipRows(out.px.data(), w, h);
  return true;
}

static bool loadRaw(const std::string &path, Image &out) {
  std::lock_guard<std::mutex> lock(gLibRawDecodeMutex);
  LibRaw raw;
  if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) return false;
  if (raw.unpack() != LIBRAW_SUCCESS) return false;
  raw.imgdata.params.output_bps = 16;
  raw.imgdata.params.gamm[0] = 1.0;
  raw.imgdata.params.gamm[1] = 1.0;
  raw.imgdata.params.no_auto_bright = 1;
  raw.imgdata.params.use_camera_wb = 1;
  raw.imgdata.params.output_color = 1;
  if (raw.dcraw_process() != LIBRAW_SUCCESS) return false;
  libraw_processed_image_t *img = raw.dcraw_make_mem_image();
  if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors < 3) {
    if (img) LibRaw::dcraw_clear_mem(img);
    return false;
  }
  const int w = img->width, h = img->height;
  out.w = w;
  out.h = h;
  out.px.resize((size_t)w * h * 4);
  const float scale = 1.0f / 65535.0f;
  if (img->bits == 16) {
    const uint16_t *p = reinterpret_cast<const uint16_t *>(img->data);
    for (int y = 0; y < h; ++y) {
      const uint16_t *src = p + (size_t)(h - 1 - y) * w * img->colors;
      float *dst = out.px.data() + (size_t)y * w * 4;
      for (int x = 0; x < w; ++x) {
        dst[0] = src[0] * scale;
        dst[1] = src[1] * scale;
        dst[2] = src[2] * scale;
        dst[3] = 1.0f;
        src += img->colors;
        dst += 4;
      }
    }
  } else {
    const uint8_t *p = img->data;
    const float s8 = 1.0f / 255.0f;
    for (int y = 0; y < h; ++y) {
      const uint8_t *src = p + (size_t)(h - 1 - y) * w * img->colors;
      float *dst = out.px.data() + (size_t)y * w * 4;
      for (int x = 0; x < w; ++x) {
        dst[0] = src[0] * s8;
        dst[1] = src[1] * s8;
        dst[2] = src[2] * s8;
        dst[3] = 1.0f;
        src += img->colors;
        dst += 4;
      }
    }
  }
  LibRaw::dcraw_clear_mem(img);
  return true;
}

static bool loadExr(const std::string &path, Image &out) {
  float *rgba = nullptr;
  int w = 0, h = 0;
  const char *err = nullptr;
  if (LoadEXR(&rgba, &w, &h, path.c_str(), &err) != TINYEXR_SUCCESS) {
    if (err) FreeEXRErrorMessage(err);
    return false;
  }
  fromRGBAFloatTopDown(rgba, w, h, out);
  free(rgba);
  return true;
}

static float halfToFloat(uint16_t h) {
#if defined(__F16C__)
  return _cvtsh_ss(h);
#else
  const uint32_t sign = (uint32_t)(h >> 15) << 31;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;
  uint32_t bits;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      exp = 127 - 15 + 1;
      while ((mant & 0x400) == 0) {
        mant <<= 1;
        --exp;
      }
      bits = sign | (exp << 23) | ((mant & 0x3ff) << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7f800000u | (mant << 13);
  } else {
    bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
  }
  float f;
  std::memcpy(&f, &bits, sizeof f);
  return f;
#endif
}

static bool loadTiffScanline(TIFF *tif, uint32_t w, uint32_t h, uint16_t bps, uint16_t spp, uint16_t sf,
                             Image &out) {
  const tsize_t rowBytes = TIFFScanlineSize(tif);
  if (rowBytes <= 0) return false;
  std::vector<uint8_t> row((size_t)rowBytes);
  out.w = (int)w;
  out.h = (int)h;
  out.px.assign((size_t)w * h * 4, 0.0f);
  for (uint32_t y = 0; y < h; ++y) {
    if (TIFFReadScanline(tif, row.data(), y, 0) < 0) return false;
    float *dst = out.px.data() + (size_t)(h - 1 - y) * w * 4;
    if (bps == 8 && sf == SAMPLEFORMAT_UINT) {
      const uint8_t *src = row.data();
      const float s = 1.0f / 255.0f;
      for (uint32_t x = 0; x < w; ++x) {
        dst[0] = src[0] * s;
        dst[1] = (spp > 1 ? src[1] : src[0]) * s;
        dst[2] = (spp > 2 ? src[2] : src[0]) * s;
        dst[3] = spp > 3 ? src[3] * s : 1.0f;
        src += spp;
        dst += 4;
      }
    } else if (bps == 16 && sf == SAMPLEFORMAT_UINT) {
      const uint16_t *src = reinterpret_cast<const uint16_t *>(row.data());
      const float s = 1.0f / 65535.0f;
      for (uint32_t x = 0; x < w; ++x) {
        dst[0] = src[0] * s;
        dst[1] = (spp > 1 ? src[1] : src[0]) * s;
        dst[2] = (spp > 2 ? src[2] : src[0]) * s;
        dst[3] = spp > 3 ? src[3] * s : 1.0f;
        src += spp;
        dst += 4;
      }
    } else if (bps == 16 && sf == SAMPLEFORMAT_IEEEFP) {
      const uint16_t *src = reinterpret_cast<const uint16_t *>(row.data());
      for (uint32_t x = 0; x < w; ++x) {
        dst[0] = halfToFloat(src[0]);
        dst[1] = halfToFloat(spp > 1 ? src[1] : src[0]);
        dst[2] = halfToFloat(spp > 2 ? src[2] : src[0]);
        dst[3] = spp > 3 ? halfToFloat(src[3]) : 1.0f;
        src += spp;
        dst += 4;
      }
    } else if (bps == 32 && sf == SAMPLEFORMAT_IEEEFP) {
      const float *src = reinterpret_cast<const float *>(row.data());
      for (uint32_t x = 0; x < w; ++x) {
        dst[0] = src[0];
        dst[1] = spp > 1 ? src[1] : src[0];
        dst[2] = spp > 2 ? src[2] : src[0];
        dst[3] = spp > 3 ? src[3] : 1.0f;
        src += spp;
        dst += 4;
      }
    } else {
      return false;
    }
  }
  return true;
}

static bool loadTiffRgba(TIFF *tif, uint32_t w, uint32_t h, Image &out) {
  std::vector<uint32_t> raster((size_t)w * h);
  if (!TIFFReadRGBAImageOriented(tif, w, h, raster.data(), ORIENTATION_TOPLEFT, 0)) return false;
  out.w = (int)w;
  out.h = (int)h;
  out.px.resize((size_t)w * h * 4);
  const float s = 1.0f / 255.0f;
  for (uint32_t y = 0; y < h; ++y) {
    float *dst = out.px.data() + (size_t)(h - 1 - y) * w * 4;
    const uint32_t *src = raster.data() + (size_t)y * w;
    for (uint32_t x = 0; x < w; ++x) {
      const uint32_t p = src[x];
      dst[0] = TIFFGetR(p) * s;
      dst[1] = TIFFGetG(p) * s;
      dst[2] = TIFFGetB(p) * s;
      dst[3] = TIFFGetA(p) * s;
      dst += 4;
    }
  }
  return true;
}

static bool loadTiff(const std::string &path, Image &out, std::vector<uint8_t> &icc, bool &isFloat) {
  icc.clear();
  isFloat = false;
  TIFF *tif = TIFFOpen(path.c_str(), "r");
  if (!tif) return false;
  uint32_t w = 0, h = 0;
  uint16_t bps = 8, spp = 3, sf = SAMPLEFORMAT_UINT, planar = PLANARCONFIG_CONTIG, orient = ORIENTATION_TOPLEFT;
  TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sf);
  TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
  TIFFGetFieldDefaulted(tif, TIFFTAG_ORIENTATION, &orient);
  uint32_t iccLen = 0;
  void *iccPtr = nullptr;
  if (TIFFGetField(tif, TIFFTAG_ICCPROFILE, &iccLen, &iccPtr) && iccPtr && iccLen > 0)
    icc.assign((const uint8_t *)iccPtr, (const uint8_t *)iccPtr + iccLen);
  isFloat = (sf == SAMPLEFORMAT_IEEEFP);
  bool ok = false;
  if (w && h && !TIFFIsTiled(tif) && planar == PLANARCONFIG_CONTIG && spp >= 1 && spp <= 4 &&
      orient == ORIENTATION_TOPLEFT &&
      ((bps == 8 && sf == SAMPLEFORMAT_UINT) || (bps == 16 && sf == SAMPLEFORMAT_UINT) ||
       (bps == 16 && sf == SAMPLEFORMAT_IEEEFP) || (bps == 32 && sf == SAMPLEFORMAT_IEEEFP))) {
    ok = loadTiffScanline(tif, w, h, bps, spp, sf, out);
  }
  if (!ok && w && h) {
    out = {};
    ok = loadTiffRgba(tif, w, h, out);
    isFloat = false;  // RGBA path is 8-bit
  }
  TIFFClose(tif);
  if (!ok) {
    out = {};
    icc.clear();
  }
  return ok;
}

static bool loadStb(const std::string &path, Image &out) {
  int w = 0, h = 0, n = 0;
  float *data = stbi_loadf(path.c_str(), &w, &h, &n, 4);
  if (!data) return false;
  fromRGBAFloatTopDown(data, w, h, out);
  stbi_image_free(data);
  return true;
}

static bool downscaleRGBA8(const unsigned char *src, int sw, int sh, int maxEdge, std::vector<unsigned char> &dst,
                           int &dw, int &dh) {
  if (sw <= 0 || sh <= 0) return false;
  const int longE = std::max(sw, sh);
  if (maxEdge <= 0 || longE <= maxEdge) {
    dw = sw;
    dh = sh;
    dst.assign(src, src + (size_t)sw * sh * 4);
    return true;
  }
  const double scale = (double)maxEdge / longE;
  dw = std::max(1, (int)std::floor(sw * scale));
  dh = std::max(1, (int)std::floor(sh * scale));
  dst.resize((size_t)dw * dh * 4);
  stbir_resize_uint8_linear(src, sw, sh, sw * 4, dst.data(), dw, dh, dw * 4, STBIR_RGBA);
  return true;
}

static bool loadStbThumbRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w, int &h) {
  int iw = 0, ih = 0, n = 0;
  unsigned char *data = stbi_load(path.c_str(), &iw, &ih, &n, 4);
  if (!data) return false;
  const bool ok = downscaleRGBA8(data, iw, ih, maxEdge, rgba, w, h);
  stbi_image_free(data);
  return ok;
}

static bool isRawExtension(const std::string &extLower) {
  return extLower == ".cr2" || extLower == ".cr3" || extLower == ".nef" || extLower == ".arw" || extLower == ".dng" ||
         extLower == ".raf" || extLower == ".orf" || extLower == ".rw2" || extLower == ".pef" || extLower == ".srw" ||
         extLower == ".raw";
}

static bool loadRawEmbeddedThumbRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w,
                                     int &h) {
  std::lock_guard<std::mutex> lock(gLibRawDecodeMutex);
  LibRaw raw;
  if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) return false;
  if (raw.unpack_thumb() != LIBRAW_SUCCESS) return false;
  const libraw_thumbnail_t &t = raw.imgdata.thumbnail;
  if (t.tlength <= 0 || !t.thumb) return false;

  std::vector<unsigned char> decoded;
  int tw = 0, th = 0;
  if (t.tformat == LIBRAW_THUMBNAIL_JPEG) {
    int n = 0;
    unsigned char *jd =
        stbi_load_from_memory(reinterpret_cast<const unsigned char *>(t.thumb), t.tlength, &tw, &th, &n, 4);
    if (!jd) return false;
    decoded.assign(jd, jd + (size_t)tw * th * 4);
    stbi_image_free(jd);
  } else if (t.tformat == LIBRAW_THUMBNAIL_BITMAP) {
    tw = t.twidth;
    th = t.theight;
    const int tc = t.tcolors >= 3 ? t.tcolors : 3;
    if (tw <= 0 || th <= 0 || tc > 4) return false;
    if ((size_t)t.tlength < (size_t)tw * th * (size_t)tc) return false;
    decoded.resize((size_t)tw * th * 4);
    const unsigned char *src = reinterpret_cast<const unsigned char *>(t.thumb);
    for (int y = 0; y < th; ++y) {
      for (int x = 0; x < tw; ++x) {
        const int si = (y * tw + x) * tc;
        const int di = (y * tw + x) * 4;
        decoded[(size_t)di] = src[si];
        decoded[(size_t)di + 1] = src[si + 1];
        decoded[(size_t)di + 2] = src[si + 2];
        decoded[(size_t)di + 3] = tc >= 4 ? src[si + 3] : 255;
      }
    }
  } else if (t.tformat == LIBRAW_THUMBNAIL_BITMAP16) {
    tw = t.twidth;
    th = t.theight;
    const int tc = t.tcolors >= 3 ? t.tcolors : 3;
    if (tw <= 0 || th <= 0 || tc > 4) return false;
    if ((size_t)t.tlength < (size_t)tw * th * (size_t)tc * 2) return false;
    decoded.resize((size_t)tw * th * 4);
    const uint16_t *src = reinterpret_cast<const uint16_t *>(t.thumb);
    for (int y = 0; y < th; ++y) {
      for (int x = 0; x < tw; ++x) {
        const int si = (y * tw + x) * tc;
        const int di = (y * tw + x) * 4;
        for (int c = 0; c < 3; ++c) decoded[(size_t)di + c] = (unsigned char)(src[si + c] >> 8);
        decoded[(size_t)di + 3] = tc >= 4 ? (unsigned char)(src[si + 3] >> 8) : 255;
      }
    }
  } else {
    return false;
  }
  return downscaleRGBA8(decoded.data(), tw, th, maxEdge, rgba, w, h);
}

static bool extractPngIcc(const std::string &path, std::vector<uint8_t> &icc);
static bool extractJpgIcc(const std::string &path, std::vector<uint8_t> &icc);
static ColorSpace classifyIcc(const std::vector<uint8_t> &icc);

bool makePreview(const Image &src, int maxEdge, Image &out) {
  if (src.w <= 0 || src.h <= 0 || src.px.empty()) return false;
  const int longEdge = std::max(src.w, src.h);
  if (maxEdge <= 0 || longEdge <= maxEdge) {
    out = src;
    return true;
  }
  const double scale = (double)maxEdge / longEdge;
  const int w = std::max(1, (int)std::floor(src.w * scale));
  const int h = std::max(1, (int)std::floor(src.h * scale));
  out.w = w;
  out.h = h;
  out.px.resize((size_t)w * h * 4);

  const unsigned int nThreads = std::min(std::max(1u, std::thread::hardware_concurrency()), 4u);
  if (nThreads > 1 && longEdge >= 512) {
    STBIR_RESIZE rs;
    stbir_resize_init(&rs, src.px.data(), src.w, src.h, 0, out.px.data(), w, h, 0, STBIR_RGBA, STBIR_TYPE_FLOAT);
    if (stbir_build_samplers_with_splits(&rs, (int)nThreads)) {
      std::vector<std::thread> threads;
      threads.reserve(nThreads);
      for (unsigned int i = 0; i < nThreads; ++i)
        threads.emplace_back([&rs, i] { stbir_resize_extended_split(&rs, (int)i, 1); });
      for (auto &t : threads) t.join();
      stbir_free_samplers(&rs);
    } else {
      stbir_resize_float_linear(src.px.data(), src.w, src.h, 0, out.px.data(), w, h, 0, STBIR_RGBA);
    }
  } else {
    stbir_resize_float_linear(src.px.data(), src.w, src.h, 0, out.px.data(), w, h, 0, STBIR_RGBA);
  }
  return true;
}

// --- lcms2 profiles ----------------------------------------------------------

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

static bool profileBytes(ColorSpace cs, std::vector<uint8_t> &out) {
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

static bool extractPngIcc(const std::string &path, std::vector<uint8_t> &icc) {
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

static bool extractJpgIcc(const std::string &path, std::vector<uint8_t> &icc) {
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

static ColorSpace classifyIcc(const std::vector<uint8_t> &icc) {
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

bool loadImage(const std::string &path, Image &out, ColorSpace &detected) {
  out = {};
  detected = ColorSpace::sRGB;
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);

  if (e == ".exr") {
    if (!loadExr(path, out)) return false;
    detected = ColorSpace::LinearRec2020;
    return true;
  }
  if (e == ".tif" || e == ".tiff") {
    std::vector<uint8_t> icc;
    bool isFloat = false;
    if (!loadTiff(path, out, icc, isFloat)) return false;
    detected = !icc.empty() ? classifyIcc(icc) : (isFloat ? ColorSpace::LinearRec2020 : ColorSpace::sRGB);
    return true;
  }

  std::vector<uint8_t> icc;
  if (e == ".png") extractPngIcc(path, icc);
  else if (e == ".jpg" || e == ".jpeg") extractJpgIcc(path, icc);

  if (loadStb(path, out)) {
    detected = !icc.empty() ? classifyIcc(icc) : ColorSpace::sRGB;
    return true;
  }
  if (loadRaw(path, out)) {
    detected = ColorSpace::LinearRec2020;
    return true;
  }
  return false;
}

void toDisplayRGBA8(const Image &img, ColorSpace space, std::vector<unsigned char> &out) {
  out.assign((size_t)img.w * img.h * 4, 0);
  if (img.w <= 0 || img.h <= 0) return;

  // Top-down float copy for the transform.
  std::vector<float> top((size_t)img.w * img.h * 4);
  for (int y = 0; y < img.h; ++y) {
    const float *src = img.px.data() + (size_t)(img.h - 1 - y) * img.w * 4;
    std::copy(src, src + img.w * 4, top.data() + (size_t)y * img.w * 4);
  }

  cmsHTRANSFORM xform = cachedTransform(space);
  if (xform) {
    cmsDoTransform(xform, top.data(), out.data(), (cmsUInt32Number)img.w * img.h);
  } else {
    // Fallback: clamp only.
    for (size_t i = 0; i < top.size(); ++i)
      out[i] = (unsigned char)std::lround(std::clamp(top[i], 0.0f, 1.0f) * 255.0f);
  }
}

bool loadThumbnailRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w, int &h) {
  if (maxEdge <= 0) maxEdge = 128;
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);

  if (isRawExtension(e)) return loadRawEmbeddedThumbRGBA(path, maxEdge, rgba, w, h);
  if (e == ".png" || e == ".jpg" || e == ".jpeg") return loadStbThumbRGBA(path, maxEdge, rgba, w, h);
  return false;
}

static void toTopDown8(const Image &img, std::vector<unsigned char> &out) {
  out.resize((size_t)img.w * img.h * 4);
  for (int y = 0; y < img.h; ++y) {
    const float *src = img.px.data() + (size_t)(img.h - 1 - y) * img.w * 4;
    unsigned char *dst = out.data() + (size_t)y * img.w * 4;
    for (int x = 0; x < img.w; ++x) {
      for (int c = 0; c < 4; ++c) {
        const float v = src[c] * 255.0f + 0.5f;
        dst[c] = v < 0.0f ? 0 : (v > 255.0f ? 255 : (unsigned char)v);
      }
      src += 4;
      dst += 4;
    }
  }
}

static uint32_t crc32_png(const uint8_t *data, size_t n) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

static void writeBe32(std::vector<uint8_t> &buf, uint32_t v) {
  buf.push_back((v >> 24) & 0xff);
  buf.push_back((v >> 16) & 0xff);
  buf.push_back((v >> 8) & 0xff);
  buf.push_back(v & 0xff);
}

static bool writePngWithIcc(const Image &img, const std::string &path, const std::vector<uint8_t> &icc) {
  std::vector<unsigned char> px8;
  toTopDown8(img, px8);
  int len = 0;
  unsigned char *png = stbi_write_png_to_mem(px8.data(), img.w * 4, img.w, img.h, 4, &len);
  if (!png || len < 8) {
    if (png) STBIW_FREE(png);
    return false;
  }
  if (icc.empty()) {
    FILE *f = fopen(path.c_str(), "wb");
    bool ok = f && fwrite(png, 1, len, f) == (size_t)len;
    if (f) fclose(f);
    STBIW_FREE(png);
    return ok;
  }

  // Insert iCCP before IEND. Profile is zlib-compressed; keyword "ICC Profile".
  uLongf bound = compressBound((uLong)icc.size());
  std::vector<uint8_t> comp(bound);
  if (compress(comp.data(), &bound, icc.data(), (uLong)icc.size()) != Z_OK) {
    STBIW_FREE(png);
    return false;
  }
  comp.resize(bound);

  const char *keyword = "ICC Profile";
  std::vector<uint8_t> chunkData;
  chunkData.insert(chunkData.end(), keyword, keyword + strlen(keyword) + 1);  // incl. NUL
  chunkData.push_back(0);  // compression method
  chunkData.insert(chunkData.end(), comp.begin(), comp.end());

  std::vector<uint8_t> typeAndData;
  typeAndData.push_back('i');
  typeAndData.push_back('C');
  typeAndData.push_back('C');
  typeAndData.push_back('P');
  typeAndData.insert(typeAndData.end(), chunkData.begin(), chunkData.end());
  const uint32_t crc = crc32_png(typeAndData.data(), typeAndData.size());

  // Find IEND (last 12 bytes of a well-formed PNG from stb).
  const size_t iend = (size_t)len - 12;
  std::vector<uint8_t> out;
  out.reserve((size_t)len + 12 + typeAndData.size());
  out.insert(out.end(), png, png + iend);
  writeBe32(out, (uint32_t)chunkData.size());
  out.insert(out.end(), typeAndData.begin(), typeAndData.end());
  writeBe32(out, crc);
  out.insert(out.end(), png + iend, png + len);
  STBIW_FREE(png);

  FILE *f = fopen(path.c_str(), "wb");
  bool ok = f && fwrite(out.data(), 1, out.size(), f) == out.size();
  if (f) fclose(f);
  return ok;
}

static bool writeJpgWithIcc(const Image &img, const std::string &path, const std::vector<uint8_t> &icc, int quality) {
  quality = std::clamp(quality, 1, 100);
  std::vector<unsigned char> px8;
  toTopDown8(img, px8);
  std::vector<unsigned char> rgb((size_t)img.w * img.h * 3);
  for (size_t i = 0, j = 0; i < px8.size(); i += 4, j += 3) {
    rgb[j] = px8[i];
    rgb[j + 1] = px8[i + 1];
    rgb[j + 2] = px8[i + 2];
  }

  std::vector<unsigned char> jpg;
  auto append = [](void *ctx, void *data, int size) {
    auto *v = static_cast<std::vector<unsigned char> *>(ctx);
    auto *p = static_cast<unsigned char *>(data);
    v->insert(v->end(), p, p + size);
  };
  if (!stbi_write_jpg_to_func(append, &jpg, img.w, img.h, 3, rgb.data(), quality) || jpg.size() < 2) return false;

  if (icc.empty()) {
    FILE *f = fopen(path.c_str(), "wb");
    bool ok = f && fwrite(jpg.data(), 1, jpg.size(), f) == jpg.size();
    if (f) fclose(f);
    return ok;
  }

  // Split ICC into APP2 segments (max payload after length field: 65533 bytes).
  // JPEG segment length includes the 2 length bytes themselves.
  const size_t header = 12 + 2;  // "ICC_PROFILE\0" + seq + count
  const size_t maxData = 65533 - header;
  const size_t nSeg = (icc.size() + maxData - 1) / maxData;
  std::vector<uint8_t> app2;
  for (size_t s = 0; s < nSeg; ++s) {
    const size_t off = s * maxData;
    const size_t n = std::min(maxData, icc.size() - off);
    const uint16_t seglen = (uint16_t)(2 + header + n);
    app2.push_back(0xff);
    app2.push_back(0xe2);  // APP2
    app2.push_back((seglen >> 8) & 0xff);
    app2.push_back(seglen & 0xff);
    const char marker[] = "ICC_PROFILE";
    app2.insert(app2.end(), marker, marker + sizeof marker);  // incl. NUL
    app2.push_back((uint8_t)(s + 1));
    app2.push_back((uint8_t)nSeg);
    app2.insert(app2.end(), icc.begin() + off, icc.begin() + off + n);
  }

  // Insert APP2 right after SOI (ffd8).
  std::vector<uint8_t> out;
  out.reserve(jpg.size() + app2.size());
  out.push_back(jpg[0]);
  out.push_back(jpg[1]);
  out.insert(out.end(), app2.begin(), app2.end());
  out.insert(out.end(), jpg.begin() + 2, jpg.end());

  FILE *f = fopen(path.c_str(), "wb");
  bool ok = f && fwrite(out.data(), 1, out.size(), f) == out.size();
  if (f) fclose(f);
  return ok;
}

bool writeImage(const Image &img, const std::string &path, ColorSpace space, int jpegQuality) {
  if (img.w <= 0 || img.h <= 0) return false;
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);

  std::vector<uint8_t> icc;
  if (!profileBytes(space, icc)) return false;

  if (e == ".png") return writePngWithIcc(img, path, icc);
  if (e == ".jpg" || e == ".jpeg") return writeJpgWithIcc(img, path, icc, jpegQuality);
  return false;
}