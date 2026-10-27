#pragma once

#include "core/Image.h"
#include "model/Chain.h"

#include <algorithm>

// Everything one render pass reads, and the one buffer it writes. A value, so a
// request captures the state it was made against instead of pointing at whatever
// the application holds when the worker wakes up.
//
// The chain and the full image are borrowed, not copied: an Effect instance is
// not copyable. A caller that mutates either one waits for the worker to go idle
// first, which is what RenderWorker::waitIdle is for.
struct RenderInputs {
  const ChainState *chain = nullptr;
  const Image *full = nullptr;
  Image *preview = nullptr;
  ColorSpace workingSpace = ColorSpace::LinearRec709;
  ColorSpace previewSpace = ColorSpace::sRGB;
  int previewMaxEdge = 0;

  // Long edge of the project, so a pass can report its proxy scale.
  int projectLongEdge() const {
    if (!full) return 0;
    return std::max(full->w, full->h);
  }

  bool hasChain() const { return chain && !chain->nodes.empty(); }
  bool hasPreview() const { return preview && !preview->px.empty(); }
  bool hasFull() const { return full && !full->px.empty(); }
};
