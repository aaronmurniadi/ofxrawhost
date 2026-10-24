#include "core/Image.h"

ColorSpace colorSpaceFromName(const std::string &name, ColorSpace fallback) {
  for (int i = 0; i < kColorSpaceCount; ++i) {
    const ColorSpace cs = static_cast<ColorSpace>(i);
    if (name == colorSpaceName(cs)) return cs;
  }
  return fallback;
}
