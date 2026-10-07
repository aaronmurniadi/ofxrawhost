#include "ChainRenderer.h"

#include "ofx/OfxHost.h"
#include "ofx/OfxMetal.h"
#include "perf.h"

#include <algorithm>
#include <chrono>
#include <cstring>

static bool anyEnabledNode(const ChainState &chain) {
  for (const auto &n : chain.nodes)
    if (n.enabled) return true;
  return false;
}

// Grows a chain-owned Metal buffer in place; releases the old one when it is too small.
static void ensureChainBuffer(void *&slot, size_t &haveBytes, size_t wantBytes) {
  if (slot && haveBytes >= wantBytes) return;
  if (slot) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(slot));
  slot = ofxMetalBufferCreate(wantBytes);
  if (slot) haveBytes = wantBytes;
  else haveBytes = 0;
}

ChainRenderer::~ChainRenderer() {
  for (int i = 0; i < 2; ++i)
    if (mtl[i]) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(mtl[i]));
}

OfxStatus ChainRenderer::render(const ChainState &chain, const std::vector<PluginEntry> &plugins, const Image &src,
                                Image &out, int gen, bool draft) {
  if (!anyEnabledNode(chain)) {
    out = src;
    return kOfxStatOK;
  }

  cur.w = src.w;
  cur.h = src.h;
  if (cur.px.size() != src.px.size()) cur.px.resize(src.px.size());
  if (!src.px.empty()) std::memcpy(cur.px.data(), src.px.data(), src.px.size() * sizeof(float));
  int cw = src.w, ch = src.h;
  float *cpuIn = cur.px.data();
  void *gpuIn = nullptr;  // when set, the pixels live in this MTLBuffer (cw x ch)
  int gpuDst = 0;         // which chain buffer the next GPU node writes into
  bool metalUsed = false;
  OfxStatus st = kOfxStatOK;

  for (size_t i = 0; i < chain.nodes.size(); ++i) {
    const Node &n = chain.nodes[i];
    if (!n.enabled) continue;
    if (!n.instance) return kOfxStatFailed;
    OfxPlugin *plugin = plugins[n.pluginIndex].plugin;
    int ow = cw, oh = ch;
    queryOutputSize(plugin, n.instance.get(), cw, ch, &ow, &oh);
    const auto t0 = std::chrono::steady_clock::now();
    if (effectUsesMetal(n.instance.get())) {
      // Keep the frame on the GPU: feed the previous node's output buffer straight
      // in and write the next one. No per-node copy and no per-node GPU stall.
      void *dstBuf = mtl[gpuDst];
      ensureChainBuffer(dstBuf, mtlBytes[gpuDst], (size_t)ow * oh * 4 * sizeof(float));
      mtl[gpuDst] = dstBuf;
      if (!dstBuf) return kOfxStatErrMemory;
      float *srcCpu = cpuIn;
      if (gpuIn) srcCpu = nullptr;
      st = renderEffect(plugin, n.instance.get(), srcCpu, nullptr, cw, ch, ow, oh, gen, gpuIn, dstBuf, draft);
      if (st != kOfxStatOK) break;
      gpuIn = dstBuf;
      cpuIn = nullptr;
      metalUsed = true;
      gpuDst = 1 - gpuDst;
    } else {
      if (gpuIn) {
        // The lazy copy is a Skyline: the GPU result must be ready before the CPU read.
        ofxMetalSync();
        cur.w = cw;
        cur.h = ch;
        const size_t need = (size_t)cw * ch * 4;
        if (cur.px.size() < need) cur.px.resize(need);
        const void *p = ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(gpuIn));
        if (!p) return kOfxStatFailed;
        std::memcpy(cur.px.data(), p, need * sizeof(float));
        gpuIn = nullptr;
        cpuIn = cur.px.data();
      }
      next.w = ow;
      next.h = oh;
      const size_t need = (size_t)ow * oh * 4;
      if (next.px.size() < need) next.px.resize(need);
      st = renderEffect(plugin, n.instance.get(), cpuIn, next.px.data(), cw, ch, ow, oh, gen, nullptr, nullptr, draft);
      if (st != kOfxStatOK) break;
      cur.swap(next);
      cpuIn = cur.px.data();
    }
    const auto t1 = std::chrono::steady_clock::now();
    perfLog(("node: " + plugins[n.pluginIndex].label).c_str(),
            std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (gen != 0 && gen != gLatestGen) {
      st = kOfxStatFailed;
      break;
    }
    cw = ow;
    ch = oh;
  }

  if (st != kOfxStatOK) {
    if (metalUsed) ofxMetalSync();  // let queued GPU work finish before the buffers are reused
    return st;
  }
  if (gpuIn) {
    ofxMetalSync();
    next.w = cw;
    next.h = ch;
    const size_t need = (size_t)cw * ch * 4;
    if (next.px.size() < need) next.px.resize(need);
    const void *p = ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(gpuIn));
    if (!p) return kOfxStatFailed;
    std::memcpy(next.px.data(), p, need * sizeof(float));
    out = std::move(next);
    return kOfxStatOK;
  }
  out = std::move(cur);
  return kOfxStatOK;
}
