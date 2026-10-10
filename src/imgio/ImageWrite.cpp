#include "imgio/ImageIO.h"
#include "imgio/ImageIOPriv.h"

#include <jxl/encode.h>
#include <jxl/thread_parallel_runner.h>
#include <tiffio.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static bool writeFileBytes(const std::string &path, const uint8_t *data, size_t n) {
  FILE *f = fopen(path.c_str(), "wb");
  bool ok = f && fwrite(data, 1, n, f) == n;
  if (f) fclose(f);
  return ok;
}

static uint8_t toByte(float v) {
  const float s = v * 255.0f + 0.5f;
  if (s < 0.0f) return 0;
  if (s > 255.0f) return 255;
  return (uint8_t)s;
}

static uint16_t toUint16(float v) {
  return (uint16_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f);
}

static void toTopDown8(const Image &img, std::vector<unsigned char> &out) {
  out.resize((size_t)img.w * img.h * 4);
  for (int y = 0; y < img.h; ++y) {
    const float *src = img.px.data() + (size_t)(img.h - 1 - y) * img.w * 4;
    unsigned char *dst = out.data() + (size_t)y * img.w * 4;
    for (int x = 0; x < img.w * 4; ++x) dst[x] = toByte(src[x]);
  }
}

static void toTopDown16(const Image &img, std::vector<uint16_t> &out) {
  out.resize((size_t)img.w * img.h * 4);
  for (int y = 0; y < img.h; ++y) {
    const float *src = img.px.data() + (size_t)(img.h - 1 - y) * img.w * 4;
    uint16_t *dst = out.data() + (size_t)y * img.w * 4;
    for (int x = 0; x < img.w * 4; ++x) dst[x] = toUint16(src[x]);
  }
}

// --- PNG ---

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

static void appendPngChunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t n) {
  writeBe32(out, (uint32_t)n);
  const size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  if (n) out.insert(out.end(), data, data + n);
  writeBe32(out, crc32_png(out.data() + start, out.size() - start));
}

// iCCP payload: keyword, NUL, compression method 0, zlib-compressed profile.
static bool pngIccpPayload(const std::vector<uint8_t> &icc, std::vector<uint8_t> &out) {
  uLongf bound = compressBound((uLong)icc.size());
  std::vector<uint8_t> comp(bound);
  if (compress(comp.data(), &bound, icc.data(), (uLong)icc.size()) != Z_OK) return false;
  comp.resize(bound);
  const char *keyword = "ICC Profile";
  out.insert(out.end(), keyword, keyword + strlen(keyword) + 1);
  out.push_back(0);
  out.insert(out.end(), comp.begin(), comp.end());
  return true;
}

static bool writePng8WithIcc(const Image &img, const std::string &path, const std::vector<uint8_t> &icc) {
  std::vector<unsigned char> px8;
  toTopDown8(img, px8);
  int len = 0;
  unsigned char *png = stbi_write_png_to_mem(px8.data(), img.w * 4, img.w, img.h, 4, &len);
  if (!png || len < 12) {
    if (png) STBIW_FREE(png);
    return false;
  }
  if (icc.empty()) {
    const bool ok = writeFileBytes(path, png, (size_t)len);
    STBIW_FREE(png);
    return ok;
  }

  // iCCP must precede the first IDAT chunk, so insert it right after IHDR.
  std::vector<uint8_t> iccp;
  if (!pngIccpPayload(icc, iccp)) {
    STBIW_FREE(png);
    return false;
  }
  const size_t afterIhdr = 8 + 25;  // signature + IHDR (4 length, 4 type, 13 data, 4 CRC)
  std::vector<uint8_t> out;
  out.reserve((size_t)len + iccp.size() + 12);
  out.insert(out.end(), png, png + afterIhdr);
  appendPngChunk(out, "iCCP", iccp.data(), iccp.size());
  out.insert(out.end(), png + afterIhdr, png + len);
  STBIW_FREE(png);
  return writeFileBytes(path, out.data(), out.size());
}

// One IDAT stream: filter byte 0 per scanline, then big-endian RGBA16 samples.
static bool deflatePng16Rows(const Image &img, std::vector<uint8_t> &idat) {
  z_stream zs{};
  if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) return false;
  std::vector<uint8_t> row((size_t)img.w * 8 + 1);
  std::vector<uint8_t> buf(65536);
  bool ok = true;
  for (int y = 0; ok && y < img.h; ++y) {
    row[0] = 0;
    const float *src = img.px.data() + (size_t)(img.h - 1 - y) * img.w * 4;
    uint8_t *dst = row.data() + 1;
    for (int i = 0; i < img.w * 4; ++i) {
      const uint16_t v = toUint16(src[i]);
      dst[0] = (uint8_t)(v >> 8);
      dst[1] = (uint8_t)(v & 0xff);
      dst += 2;
    }
    zs.next_in = row.data();
    zs.avail_in = (uInt)row.size();
    while (ok && zs.avail_in > 0) {
      zs.next_out = buf.data();
      zs.avail_out = (uInt)buf.size();
      const int rc = deflate(&zs, Z_NO_FLUSH);
      if (rc != Z_OK) {
        ok = false;
        break;
      }
      idat.insert(idat.end(), buf.data(), buf.data() + (buf.size() - zs.avail_out));
    }
  }
  while (ok) {
    zs.next_out = buf.data();
    zs.avail_out = (uInt)buf.size();
    const int rc = deflate(&zs, Z_FINISH);
    idat.insert(idat.end(), buf.data(), buf.data() + (buf.size() - zs.avail_out));
    if (rc == Z_STREAM_END) break;
    if (rc != Z_OK) ok = false;
  }
  deflateEnd(&zs);
  return ok;
}

static bool writePng16WithIcc(const Image &img, const std::string &path, const std::vector<uint8_t> &icc) {
  static const uint8_t signature[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  std::vector<uint8_t> out(signature, signature + 8);

  std::vector<uint8_t> ihdr;
  writeBe32(ihdr, (uint32_t)img.w);
  writeBe32(ihdr, (uint32_t)img.h);
  ihdr.push_back(16);  // bit depth
  ihdr.push_back(6);   // color type: RGBA
  ihdr.push_back(0);   // compression: deflate
  ihdr.push_back(0);   // filter method
  ihdr.push_back(0);   // interlace: none
  appendPngChunk(out, "IHDR", ihdr.data(), ihdr.size());

  if (!icc.empty()) {
    std::vector<uint8_t> iccp;
    if (!pngIccpPayload(icc, iccp)) return false;
    appendPngChunk(out, "iCCP", iccp.data(), iccp.size());
  }

  std::vector<uint8_t> idat;
  if (!deflatePng16Rows(img, idat)) return false;
  appendPngChunk(out, "IDAT", idat.data(), idat.size());
  appendPngChunk(out, "IEND", nullptr, 0);
  return writeFileBytes(path, out.data(), out.size());
}

// --- TIFF ---

static bool writeTiff(const Image &img, const std::string &path, const std::vector<uint8_t> &icc, int bitDepth) {
  TIFF *tif = TIFFOpen(path.c_str(), "w");
  if (!tif) return false;
  const uint32_t w = (uint32_t)img.w, h = (uint32_t)img.h;
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, w);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 4);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, (uint16_t)bitDepth);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  if (!icc.empty()) TIFFSetField(tif, TIFFTAG_ICCPROFILE, (uint32_t)icc.size(), icc.data());

  std::vector<uint16_t> row16;
  std::vector<uint8_t> row8;
  if (bitDepth == 16) row16.resize((size_t)w * 4);
  else row8.resize((size_t)w * 4);
  for (uint32_t y = 0; y < h; ++y) {
    const float *src = img.px.data() + (size_t)(img.h - 1 - (int)y) * img.w * 4;
    void *row = nullptr;
    if (bitDepth == 16) {
      for (uint32_t i = 0; i < w * 4; ++i) row16[i] = toUint16(src[i]);
      row = row16.data();
    } else {
      for (uint32_t i = 0; i < w * 4; ++i) row8[i] = toByte(src[i]);
      row = row8.data();
    }
    if (TIFFWriteScanline(tif, row, y, 0) < 0) {
      TIFFClose(tif);
      return false;
    }
  }
  TIFFClose(tif);
  return true;
}

// --- JPEG ---

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

  if (icc.empty()) return writeFileBytes(path, jpg.data(), jpg.size());

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
  return writeFileBytes(path, out.data(), out.size());
}

// --- WebP ---

static bool writeWebpWithIcc(const Image &img, const std::string &path, const std::vector<uint8_t> &icc, int quality,
                             bool lossless) {
  if (img.w > WEBP_MAX_DIMENSION || img.h > WEBP_MAX_DIMENSION) return false;
  std::vector<unsigned char> px8;
  toTopDown8(img, px8);
  const int stride = img.w * 4;

  uint8_t *webp = nullptr;
  size_t size = 0;
  if (lossless) size = WebPEncodeLosslessRGBA(px8.data(), img.w, img.h, stride, &webp);
  else size = WebPEncodeRGBA(px8.data(), img.w, img.h, stride, (float)std::clamp(quality, 1, 100), &webp);
  if (size == 0 || !webp) {
    if (webp) WebPFree(webp);
    return false;
  }
  if (icc.empty()) {
    const bool ok = writeFileBytes(path, webp, size);
    WebPFree(webp);
    return ok;
  }

  // Adding ICCP promotes the simple lossy/lossless bitstream to the extended format.
  const WebPData bitstream = {webp, size};
  WebPMux *mux = WebPMuxCreate(&bitstream, 1);
  if (!mux) {
    WebPFree(webp);
    return false;
  }
  const WebPData iccChunk = {icc.data(), icc.size()};
  bool ok = WebPMuxSetChunk(mux, "ICCP", &iccChunk, 0) == WEBP_MUX_OK;
  WebPData assembled = {nullptr, 0};
  if (ok) ok = WebPMuxAssemble(mux, &assembled) == WEBP_MUX_OK;
  if (ok) ok = writeFileBytes(path, assembled.bytes, assembled.size);
  WebPDataClear(&assembled);
  WebPMuxDelete(mux);
  WebPFree(webp);
  return ok;
}

// --- JPEG XL ---

// JPEG XL distance: 0 is lossless, 1 is a common visually lossless default.
static float jxlDistanceFromQuality(int quality) {
  const float d = (float)(100 - std::clamp(quality, 1, 100)) / 10.0f;
  if (d < 0.1f) return 0.1f;
  return d;
}

static bool writeJxl(const Image &img, const std::string &path, const std::vector<uint8_t> &icc,
                     const EncodeOptions &opts) {
  uint32_t bitDepth = 8;
  if (opts.bitDepth >= 16) bitDepth = 16;

  JxlEncoder *enc = JxlEncoderCreate(nullptr);
  if (!enc) return false;
  void *runner = JxlThreadParallelRunnerCreate(nullptr, JxlThreadParallelRunnerDefaultNumWorkerThreads());
  bool ok = runner != nullptr;
  if (ok) ok = JxlEncoderUseContainer(enc, JXL_TRUE) == JXL_ENC_SUCCESS;
  if (ok) ok = JxlEncoderSetParallelRunner(enc, JxlThreadParallelRunner, runner) == JXL_ENC_SUCCESS;

  JxlBasicInfo info;
  JxlEncoderInitBasicInfo(&info);
  info.xsize = (uint32_t)img.w;
  info.ysize = (uint32_t)img.h;
  info.bits_per_sample = bitDepth;
  info.exponent_bits_per_sample = 0;
  info.num_extra_channels = 1;
  info.alpha_bits = bitDepth;
  info.alpha_exponent_bits = 0;
  info.alpha_premultiplied = JXL_FALSE;
  info.uses_original_profile = JXL_TRUE;
  if (ok) ok = JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS;

  JxlExtraChannelInfo alpha;
  JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &alpha);
  alpha.bits_per_sample = bitDepth;
  alpha.exponent_bits_per_sample = 0;
  alpha.alpha_premultiplied = JXL_FALSE;
  if (ok) ok = JxlEncoderSetExtraChannelInfo(enc, 0, &alpha) == JXL_ENC_SUCCESS;
  if (ok && !icc.empty()) ok = JxlEncoderSetICCProfile(enc, icc.data(), icc.size()) == JXL_ENC_SUCCESS;

  JxlEncoderFrameSettings *frame = nullptr;
  if (ok) frame = JxlEncoderFrameSettingsCreate(enc, nullptr);
  if (ok && !frame) ok = false;
  if (ok) {
    JXL_BOOL lossless = JXL_FALSE;
    if (opts.lossless) lossless = JXL_TRUE;
    ok = JxlEncoderSetFrameLossless(frame, lossless) == JXL_ENC_SUCCESS;
  }
  if (ok && !opts.lossless) ok = JxlEncoderSetFrameDistance(frame, jxlDistanceFromQuality(opts.quality)) == JXL_ENC_SUCCESS;

  std::vector<uint16_t> px16;
  std::vector<uint8_t> px8;
  const void *pixels = nullptr;
  size_t bytes = 0;
  if (bitDepth == 16) {
    toTopDown16(img, px16);
    pixels = px16.data();
    bytes = px16.size() * sizeof(uint16_t);
  } else {
    toTopDown8(img, px8);
    pixels = px8.data();
    bytes = px8.size();
  }

  JxlPixelFormat format = {};
  format.num_channels = 4;
  format.endianness = JXL_NATIVE_ENDIAN;
  format.align = 0;
  if (bitDepth == 16) format.data_type = JXL_TYPE_UINT16;
  else format.data_type = JXL_TYPE_UINT8;

  if (ok) ok = JxlEncoderAddImageFrame(frame, &format, pixels, bytes) == JXL_ENC_SUCCESS;
  std::vector<uint8_t> out;
  if (ok) {
    JxlEncoderCloseInput(enc);
    std::vector<uint8_t> buf(65536);
    JxlEncoderStatus st = JXL_ENC_NEED_MORE_OUTPUT;
    while (ok && st == JXL_ENC_NEED_MORE_OUTPUT) {
      uint8_t *next = buf.data();
      size_t avail = buf.size();
      st = JxlEncoderProcessOutput(enc, &next, &avail);
      out.insert(out.end(), buf.data(), next);
      if (st == JXL_ENC_ERROR) ok = false;
    }
    if (st != JXL_ENC_SUCCESS) ok = false;
  }
  if (ok) ok = writeFileBytes(path, out.data(), out.size());

  if (runner) JxlThreadParallelRunnerDestroy(runner);
  JxlEncoderDestroy(enc);
  return ok;
}

bool writeImage(const Image &img, const std::string &path, ColorSpace space, EncodeOptions opts) {
  if (img.w <= 0 || img.h <= 0) return false;
  const std::string e = lowerFileExtension(path);

  std::vector<uint8_t> icc;
  if (!profileBytes(space, icc)) return false;

  if (e == ".png") {
    if (opts.bitDepth >= 16) return writePng16WithIcc(img, path, icc);
    return writePng8WithIcc(img, path, icc);
  }
  if (e == ".jpg" || e == ".jpeg") return writeJpgWithIcc(img, path, icc, opts.quality);
  if (e == ".tif" || e == ".tiff") {
    int bitDepth = 8;
    if (opts.bitDepth >= 16) bitDepth = 16;
    return writeTiff(img, path, icc, bitDepth);
  }
  if (e == ".webp") return writeWebpWithIcc(img, path, icc, opts.quality, opts.lossless);
  if (e == ".jxl") return writeJxl(img, path, icc, opts);
  return false;
}
