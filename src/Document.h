// The open image and its workspace context. A plain data holder: the document
// commands live in Actions.cpp / DocumentActions.cpp.
#pragma once

#include "imgio/ImageIO.h"

#include <string>

struct DocumentState {
  std::string path;
  std::string workspaceDir;
  Image full, preview;
  ColorSpace inputSpace = ColorSpace::LinearRec2020;
};
