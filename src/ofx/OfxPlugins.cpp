#include "ofx/OfxHost.h"
#include "ofx/OfxHostPriv.h"
#include "ofx/OfxMetal.h"
#include "ofxGPURender.h"

#include "ofxParam.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

static void *loadLib(const fs::path &path) {
#ifdef _WIN32
  return (void *)LoadLibraryW(path.wstring().c_str());
#else
  return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}
static void *sym(void *lib, const char *name) {
#ifdef _WIN32
  return (void *)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}
static const char *loadErr() {
#ifdef _WIN32
  static char buf[64];
  snprintf(buf, sizeof buf, "Win32 error %lu", GetLastError());
  return buf;
#else
  return dlerror();
#endif
}

// ------------------------------------------------------------- plugin loading

std::vector<PluginEntry> gPlugins;

OfxStatus callAction(OfxPlugin *p, const char *action, Effect *e, PropSet *in) {
  return p->mainEntry(action, e, in ? H(in) : nullptr, nullptr);
}
bool ofxActionOk(OfxStatus s) { return s == kOfxStatOK || s == kOfxStatReplyDefault; }

static fs::path pluginBinary(const fs::path &bundle) {
  const fs::path contents = bundle / "Contents";
  const std::string stem = bundle.stem().string();  // Foo.ofx
#if defined(_WIN32)
  const char *arch = sizeof(void *) == 8 ? "Win64" : "Win32";
#elif defined(__APPLE__)
  const char *arch = "MacOS";
#elif defined(__aarch64__) || defined(__arm64__)
  const char *arch = "Linux-arm-64";
#elif defined(__x86_64__)
  const char *arch = "Linux-x86-64";
#else
  const char *arch = "Linux-x86";
#endif
  fs::path bin = contents / arch / stem;
  if (fs::exists(bin)) return bin;
#ifdef __APPLE__
  // Universal / arm64 / x86_64 subdirs used by some vendors.
  for (const char *sub : {"MacOS/arm64", "MacOS/x86_64", "MacOS/universal"}) {
    bin = contents / sub / stem;
    if (fs::exists(bin)) return bin;
  }
#endif
  return contents / arch / stem;
}

static std::string pluginAuthor(OfxPlugin *p, const Effect &desc) {
  const std::string grouping = sprop(desc.props, kOfxImageEffectPluginPropGrouping);
  if (!grouping.empty()) return grouping;
  const char *id = p && p->pluginIdentifier ? p->pluginIdentifier : "";
  const std::string s = id;
  const size_t d1 = s.find('.');
  if (d1 == std::string::npos) return s.empty() ? "Other" : s;
  const size_t d2 = s.find('.', d1 + 1);
  if (d2 == std::string::npos) return s.substr(0, d1);
  return s.substr(0, d2);
}

static void loadBundle(const fs::path &bundle) {
  const fs::path bin = pluginBinary(bundle);
  void *lib = loadLib(bin);
  if (!lib) {
    fprintf(stderr, "Skipping %s: %s\n", bundle.string().c_str(), loadErr());
    return;
  }
  auto setHost = reinterpret_cast<OfxStatus (*)(const OfxHost *)>(sym(lib, "OfxSetHost"));
  auto count = reinterpret_cast<int (*)()>(sym(lib, "OfxGetNumberOfPlugins"));
  auto get = reinterpret_cast<OfxPlugin *(*)(int)>(sym(lib, "OfxGetPlugin"));
  if (!count || !get) return;
  if (setHost) setHost(&gOfxHost);
  for (int i = 0, n = count(); i < n; ++i) {
    OfxPlugin *p = get(i);
    if (!p || !p->setHost || !p->mainEntry || strcmp(p->pluginApi, kOfxImageEffectPluginApi) != 0) continue;
    p->setHost(&gOfxHost);
    if (!ofxActionOk(callAction(p, kOfxActionLoad, nullptr))) continue;
     auto base = std::make_unique<Effect>();
     propSetString(H(&base->props), kOfxPropType, 0, kOfxTypeImageEffect);
     if (!ofxActionOk(callAction(p, kOfxActionDescribe, base.get()))) continue;
    const bool metalCapable = sprop(base->props, kOfxImageEffectPropMetalRenderSupported) == "true";
    if (metalCapable) std::fprintf(stderr, "[metal] plugin '%s' declares Metal render support\n", p->pluginIdentifier);
    bool filter = false;
    for (auto &v : base->props.m[kOfxImageEffectPropSupportedContexts]) filter |= v.s == kOfxImageEffectContextFilter;
    if (!filter) continue;
    auto ctx = cloneEffect(*base);
    PropSet in;
    propSetString(H(&in), kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
    if (!ofxActionOk(callAction(p, kOfxImageEffectActionDescribeInContext, ctx.get(), &in))) continue;
    const std::string label = sprop(base->props, kOfxPropLabel);
    const std::string author = pluginAuthor(p, *base);
    gPlugins.push_back({p, label.empty() ? p->pluginIdentifier : label, author, std::move(ctx), metalCapable});
  }
}

void loadPlugins() {
  std::vector<std::string> dirs;
  if (const char *env = getenv("OFX_PLUGIN_PATH")) {
    std::string s = env;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    for (size_t start = 0, end; start <= s.size(); start = end + 1) {
      end = std::min(s.find(sep, start), s.size());
      if (end > start) dirs.push_back(s.substr(start, end - start));
    }
  }
#if defined(_WIN32)
  dirs.push_back("C:\\Program Files\\Common Files\\OFX\\Plugins");
#elif defined(__APPLE__)
  dirs.push_back("/Library/OFX/Plugins");
#else
  dirs.push_back("/usr/OFX/Plugins");
  dirs.push_back("/usr/local/OFX/Plugins");
#endif
  for (auto &d : dirs) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const fs::path &path = it->path();
      if (path.extension() == ".bundle" && path.stem().extension() == ".ofx") {
        loadBundle(path);
        it.disable_recursion_pending();
      }
    }
  }
  std::sort(gPlugins.begin(), gPlugins.end(), [](const PluginEntry &a, const PluginEntry &b) {
    if (a.author != b.author) return a.author < b.author;
    return a.label < b.label;
  });
}

std::unique_ptr<Effect> createInstance(PluginEntry &pe) {
  auto e = cloneEffect(*pe.descriptor);
  e->metalCapable = pe.metalCapable;
  OfxPropertySetHandle ep = H(&e->props);
  propSetString(ep, kOfxPropType, 0, kOfxTypeImageEffectInstance);
  propSetString(ep, kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
  propSetInt(ep, kOfxPropIsInteractive, 0, 1);
  propSetPointer(ep, kOfxPropInstanceData, 0, nullptr);
  for (auto &p : e->params) {
    p->v.resize(dims(p->type));
    for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = dprop(p->props, kOfxParamPropDefault, (int)i, 0);
    if (isStringType(p->type)) p->s = sprop(p->props, kOfxParamPropDefault);
  }
  const double range[2] = {0, 0};
  for (auto &c : e->clips) {
    OfxPropertySetHandle cp = H(&c->props);
    propSetString(cp, kOfxImageEffectPropPixelDepth, 0, kOfxBitDepthFloat);
    propSetString(cp, kOfxImageClipPropUnmappedPixelDepth, 0, kOfxBitDepthFloat);
    propSetString(cp, kOfxImageEffectPropComponents, 0, kOfxImageComponentRGBA);
    propSetString(cp, kOfxImageClipPropUnmappedComponents, 0, kOfxImageComponentRGBA);
    propSetString(cp, kOfxImageEffectPropPreMultiplication, 0, kOfxImageOpaque);
    propSetString(cp, kOfxImageClipPropFieldOrder, 0, kOfxImageFieldNone);
    propSetDouble(cp, kOfxImagePropPixelAspectRatio, 0, 1);
    propSetDouble(cp, kOfxImageEffectPropFrameRate, 0, 24);
    propSetN<double, propSetDouble>(cp, kOfxImageEffectPropFrameRange, 2, range);
    propSetN<double, propSetDouble>(cp, kOfxImageEffectPropUnmappedFrameRange, 2, range);
    propSetInt(cp, kOfxImageClipPropConnected, 0, 1);
    propSetInt(cp, kOfxImageClipPropContinuousSamples, 0, 0);
  }
  if (!ofxActionOk(callAction(pe.plugin, kOfxActionCreateInstance, e.get()))) return nullptr;
  return e;
}

OfxStatus renderEffect(OfxPlugin *plugin, Effect *e, float *src, float *dst, int w, int h, int gen) {
  e->src = src;
  e->dst = dst;
  e->w = w;
  e->h = h;
  e->renderGen = gen;
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const int window[4] = {0, 0, w, h};
  const double scale[2] = {1, 1};
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetString(a, kOfxImageEffectPropFieldToRender, 0, kOfxImageFieldNone);
  propSetN<int, propSetInt>(a, kOfxImageEffectPropRenderWindow, 4, window);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  propSetInt(a, kOfxImageEffectPropSequentialRenderStatus, 0, 0);
  propSetInt(a, kOfxImageEffectPropInteractiveRenderStatus, 0, gen != 0);
  propSetInt(a, kOfxImageEffectPropRenderQualityDraft, 0, 0);
  e->metalEnabled = e->metalCapable && ofxMetalAvailable();
  if (e->metalEnabled) {
    propSetInt(a, kOfxImageEffectPropMetalEnabled, 0, 1);
    propSetPointer(a, kOfxImageEffectPropMetalCommandQueue, 0, ofxMetalCommandQueue());
    if (!e->srcMtl) e->srcMtl = ofxMetalBufferCreate((size_t)w * h * 4 * sizeof(float));
    if (!e->dstMtl) e->dstMtl = ofxMetalBufferCreate((size_t)w * h * 4 * sizeof(float));
    if (e->srcMtl && src)
      std::memcpy(ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(e->srcMtl)), src, (size_t)w * h * 4 * sizeof(float));
  } else {
    propSetInt(a, kOfxImageEffectPropMetalEnabled, 0, 0);
  }
  const OfxStatus st = callAction(plugin, kOfxImageEffectActionRender, e, &in);
  if (e->metalEnabled) ofxMetalSync();
  if (e->metalEnabled && st == kOfxStatOK && e->dstMtl) {
    float *d = static_cast<float *>(ofxMetalBufferContents(reinterpret_cast<OfxMetalBuffer *>(e->dstMtl)));
    if (d && e->dst) std::memcpy(e->dst, d, (size_t)w * h * 4 * sizeof(float));
  }
  if (st != kOfxStatOK && e->metalCapable) {
    std::fprintf(stderr, "[metal] render failed: status=%d (0x%08x) metal=%d plugin=%p\n", (int)st, (unsigned int)st, (int)e->metalEnabled, (void*)plugin);
  }
  e->src = e->dst = nullptr;
  if (e->srcMtl) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(e->srcMtl));
  if (e->dstMtl) ofxMetalBufferRelease(reinterpret_cast<OfxMetalBuffer *>(e->dstMtl));
  e->srcMtl = e->dstMtl = nullptr;
  e->metalEnabled = false;
  return st;
}

