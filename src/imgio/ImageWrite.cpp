#include "imgio/ImageIO.h"
#include "imgio/ImageIOPriv.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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