// Pure node-chain render engine. Takes the chain, the plugin table, and the
// source image; owns its ping-pong buffers. No dependency on App or the UI, so
// it can be exercised on its own.
#pragma once

#include "Chain.h"
#include "imgio/ImageIO.h"
#include "ofx/OfxRegistry.h"
#include "ofxCore.h"

#include <cstddef>
#include <vector>

// Runs the enabled nodes in order. Owns the two ping-pong buffers, so one caller
// can render many times without a thread_local. Give each rendering thread its own.
struct ChainRenderer {
  Image cur, next;
  void *mtl[2] = {nullptr, nullptr};  // id<MTLBuffer> chain ping-pong (may stay null)
  size_t mtlBytes[2] = {0, 0};        // capacities of mtl[0] and mtl[1]
  ChainRenderer() = default;
  ~ChainRenderer();
  ChainRenderer(const ChainRenderer &) = delete;
  ChainRenderer &operator=(const ChainRenderer &) = delete;

  // src: bottom-up float RGBA. out receives the last node's output.
  // gen is the abort generation; draft requests reduced-quality interactive passes.
  OfxStatus render(const ChainState &chain, const std::vector<PluginEntry> &plugins, const Image &src, Image &out,
                   int gen, bool draft = false);
};
