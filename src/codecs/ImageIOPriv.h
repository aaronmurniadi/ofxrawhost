#pragma once

#include "imgio/ImageIO.h"

#include <cstdint>
#include <string>
#include <vector>

// Shared between ImageLoad / ImageColor / ImageWrite translation units.
bool fromRGBAFloatTopDown(float *src, int w, int h, Image &out);
void flipRows(float *px, int w, int h);
bool profileBytes(ColorSpace cs, std::vector<uint8_t> &out);
bool extractPngIcc(const std::string &path, std::vector<uint8_t> &icc);
bool extractJpgIcc(const std::string &path, std::vector<uint8_t> &icc);
ColorSpace classifyIcc(const std::vector<uint8_t> &icc);
