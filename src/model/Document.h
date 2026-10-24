// The open image and its workspace context. A plain data holder: the document
// commands live in Actions.cpp, and its capture and restore in Session.cpp.
#pragma once

#include "imgio/ImageIO.h"

#include <string>

struct DocumentState {
  std::string path;
  std::string workspaceDir;
  Image full, preview;
  // Space the file declares (tag or ICC). Metadata: it travels in the sidecar
  // and in the export metadata, and it never drives the pixels.
  ColorSpace fileSpace = ColorSpace::LinearRec2020;
  // Space of the decoded full.px buffer. It differs from fileSpace when the
  // decoder removed the transfer curve: RAW, and PNG/JPEG through stb.
  ColorSpace workingSpace = ColorSpace::LinearRec709;
  // Space handed to the first plugin in the chain. It starts at workingSpace,
  // and the Input colorspace control can override it for this image.
  ColorSpace inputSpace = ColorSpace::LinearRec709;
  // Camera and ISO of the open frame, when the decoder found them (RAW only).
  // The Denoise plugin turns them into a sensor noise model.
  ImageMeta meta;
};
