#include "ImageIO.h"

#include <libraw/libraw.h>
#include <lcms2.h>
#include <tiffio.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
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
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include "tinyexr.h"

namespace fs = std::filesystem;

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

static bool loadTiff(const std::string &path, Image &out) {
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
  }
  TIFFClose(tif);
  if (!ok) out = {};
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

bool loadImage(const std::string &path, Image &out) {
  out = {};
  std::string e = fs::path(path).extension().string();
  for (char &c : e) c = (char)tolower((unsigned char)c);
  if (e == ".exr") return loadExr(path, out);
  if (e == ".tif" || e == ".tiff") return loadTiff(path, out);
  if (loadStb(path, out)) return true;
  return loadRaw(path, out);
}

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