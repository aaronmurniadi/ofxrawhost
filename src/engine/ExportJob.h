// One export: everything the worker needs, captured off the live state before
// the thread starts.
//
// Nothing here points back into the composition root: the interface keeps
// running and may swap the document while the export is in flight.

#pragma once

#include "core/Image.h"
#include "persist/ProjectPersist.h"

#include <string>

struct ExportJob {
  Image src;  // full-resolution source
  std::string outPath;
  std::string sourcePath;
  int previewW = 0;  // to put the instances back afterwards
  int previewH = 0;
  ColorSpace chainSpace = ColorSpace::sRGB;   // space the chain writes
  ColorSpace exportSpace = ColorSpace::sRGB;  // space the file is encoded in
  ColorSpace fileSpace = ColorSpace::LinearRec2020;
  RenderIntent intent = RenderIntent::Perceptual;
  EncodeOptions opts;
  PersistChain chain;
  bool writeSidecar = false;
};

struct ExportResult {
  bool ok = false;
  std::string outPath;
  std::string message;  // ready for the status bar
};
