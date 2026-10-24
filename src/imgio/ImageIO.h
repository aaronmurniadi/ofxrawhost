// Decode RAW (LibRaw) / raster (stb), convert to/from OFX float buffers, export.
#pragma once

#include <string>
#include <vector>

#include "core/Image.h"
#include "imgio/FileFormats.h"

// Loads RAW via LibRaw (linear, camera WB), TIFF via libtiff, or PNG/JPEG/EXR via stb/tinyexr.
// detected: inferred Input Color Space (RAW / untagged float → Linear Rec.2020; untagged LDR → sRGB;
// embedded ICC → nearest of the four tags). Does not convert pixels.
// working: color space of the returned buffer. It equals detected when the
// samples are stored as they are (integer TIFF, EXR), and the linear counterpart
// of detected when the decoder removed the transfer curve (RAW, PNG/JPEG).
// The preview display and the plugin Input Color Space read working, while
// detected describes the file and travels in the sidecar and the export metadata.
// meta, when given, receives the camera and ISO the decoder found.
bool loadImage(const std::string &path, Image &out, ColorSpace &detected, ColorSpace &working,
               ImageMeta *meta = nullptr);
// maxEdge 0 = full size; otherwise downsamples so longest edge <= maxEdge.
bool makePreview(const Image &src, int maxEdge, Image &out);

// Format from path extension; PNG/JPEG/TIFF/WebP/JPEG XL embed ICC.
bool writeImage(const Image &img, const std::string &path, ColorSpace space = ColorSpace::sRGB, EncodeOptions opts = {});
// Converts an image in place between two color spaces with the given rendering
// intent. Returns false when a profile or a transform cannot be built, and then
// leaves the pixels untouched.
bool convertColorSpace(Image &img, ColorSpace from, ColorSpace to, RenderIntent intent);
// Top-down 8-bit RGBA of img, converted from `from` into `to` first. The bytes
// are the encoding of `to`, so they match what a file tagged `to` holds: a
// linear space gives the dark mid-tones such a file shows in a viewer without
// color management. The preview uses this with the preview color space.
void toDisplayRGBA8(const Image &img, ColorSpace from, ColorSpace to, std::vector<unsigned char> &out);
// Small filmstrip preview (downscaled source, sRGB 8-bit RGBA).
bool loadThumbnailRGBA(const std::string &path, int maxEdge, std::vector<unsigned char> &rgba, int &w, int &h);
