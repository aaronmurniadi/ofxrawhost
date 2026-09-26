#include "UI.h"

#include "ImageIO.h"
#include "OfxHost.h"
#include "ofxParam.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "portable-file-dialogs.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include "MacPinch.h"
#endif

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static const char *kOutputSpaces[] = {"sRGB", "Display P3", "Linear Rec.709", "Linear Rec.2020"};

// Long-edge caps for 16:9 frames; 0 = no downscale.
static const struct {
  const char *label;
  int maxEdge;
} kPreviewRes[] = {
  {"720p", 1280},
  {"1080p", 1920},
  {"1440p", 2560},
  {"Full res", 0},
};
static constexpr int kPreviewResCount = 4;

static ColorSpace outputSpace(int index) {
  index = std::clamp(index, 0, 3);
  return static_cast<ColorSpace>(index);
}

struct Node {
  int pluginIndex = -1;
  bool enabled = true;
  std::unique_ptr<Effect> instance;
  std::map<std::string, bool> groupOpen;
};

struct App {
  GLFWwindow *window = nullptr;
  unsigned int tex = 0;
  int texW = 0, texH = 0;

  Image full, preview;
  std::string path, status = "Open an image. Source is fed to the plugin as scene-linear.";
  int outputIndex = 0;
  int exportFormat = 1;  // JPEG
  int jpegQuality = 92;
  int previewRes = 1;  // 1080p
  bool showLeft = true;
  bool showRight = true;
  float leftW = 280.0f;
  float rightW = 420.0f;
  char paramFilter[128] = {};
  char pluginFilter[128] = {};
  float previewZoom = 1.0f;  // 1 = fit in view
  ImVec2 previewPan = {0, 0};
  std::vector<Node> nodes;
  int selectedNode = -1;

  std::mutex renderMutex;
  std::condition_variable renderCv;
  std::atomic<bool> quit{false};
  std::atomic<bool> renderPending{false};
  std::thread renderThread;
  Image display;  // latest rendered (bottom-up float), guarded by displayMutex
  std::mutex displayMutex;
  bool displayDirty = false;
  int displayGen = 0;

  std::mutex statusMutex;
  void setStatus(const std::string &s) {
    std::lock_guard<std::mutex> lock(statusMutex);
    status = s;
  }
  std::string getStatus() {
    std::lock_guard<std::mutex> lock(statusMutex);
    return status;
  }
};

static Node *selectedNode(App &app) {
  if (app.selectedNode < 0 || app.selectedNode >= (int)app.nodes.size()) return nullptr;
  return &app.nodes[app.selectedNode];
}

static const std::vector<Val> &choiceOptions(Param *p) {
  static const std::vector<Val> none;
  auto it = p->props.m.find(kOfxParamPropChoiceOption);
  return it != p->props.m.end() ? it->second : none;
}

static void notifyChanged(App &app, Node &node, Param *p) {
  PropSet in;
  OfxPropertySetHandle a = H(&in);
  const double scale[2] = {1, 1};
  propSetString(a, kOfxPropType, 0, kOfxTypeParameter);
  propSetString(a, kOfxPropName, 0, p->name.c_str());
  propSetString(a, kOfxPropChangeReason, 0, kOfxChangeUserEdited);
  propSetDouble(a, kOfxPropTime, 0, 0);
  propSetN<double, propSetDouble>(a, kOfxImageEffectPropRenderScale, 2, scale);
  OfxPlugin *plugin = gPlugins[node.pluginIndex].plugin;
  callAction(plugin, kOfxActionBeginInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionInstanceChanged, node.instance.get(), &in);
  callAction(plugin, kOfxActionEndInstanceChanged, node.instance.get(), &in);
}

static void applyColorDefaults(App &app, Node &node) {
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->type != kOfxParamTypeChoice) continue;
    const std::string label = sprop(p->props, kOfxPropLabel);
    const char *want = label == "Input Color Space" ? "Linear Rec.709" : label == "Output Color Space" ? "sRGB" : nullptr;
    if (!want) continue;
    const auto &options = choiceOptions(p);
    for (size_t i = 0; i < options.size(); ++i) {
      if (options[i].s != want) continue;
      {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[0] = (double)i;
      }
      notifyChanged(app, node, p);
      break;
    }
  }
}

static void syncOutputTag(App &app) {
  // Prefer the last node in the chain that exposes an Output Color Space choice.
  for (int n = (int)app.nodes.size() - 1; n >= 0; --n) {
    for (auto &up : app.nodes[n].instance->params) {
      Param *p = up.get();
      if (p->type != kOfxParamTypeChoice || sprop(p->props, kOfxPropLabel) != "Output Color Space" ||
          dprop(p->props, kOfxParamPropSecret, 0, 0) != 0)
        continue;
      const auto &options = choiceOptions(p);
      const size_t index = (size_t)p->v[0];
      if (index < options.size()) {
        for (int i = 0; i < 4; ++i)
          if (options[index].s == kOutputSpaces[i]) {
            app.outputIndex = i;
            return;
          }
      }
    }
  }
}

static void waitRenderIdle(App &app) {
  ++gLatestGen;
  std::unique_lock<std::mutex> lock(app.renderMutex);
  app.renderPending = false;
}

static void showSourcePreview(App &app) {
  if (app.preview.px.empty()) return;
  std::lock_guard<std::mutex> lock(app.displayMutex);
  app.display = app.preview;
  app.displayDirty = true;
}

static void scheduleRender(App &app) {
  if (app.nodes.empty() || app.preview.px.empty()) {
    showSourcePreview(app);
    return;
  }
  for (auto &n : app.nodes) {
    if (n.instance) {
      n.instance->w = app.preview.w;
      n.instance->h = app.preview.h;
    }
  }
  app.renderPending = true;
  app.renderCv.notify_one();
}

static void destroyNode(App &app, int index) {
  if (index < 0 || index >= (int)app.nodes.size()) return;
  waitRenderIdle(app);
  Node &n = app.nodes[index];
  if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.nodes.erase(app.nodes.begin() + index);
  if (app.nodes.empty())
    app.selectedNode = -1;
  else if (app.selectedNode >= (int)app.nodes.size())
    app.selectedNode = (int)app.nodes.size() - 1;
  else if (app.selectedNode > index)
    --app.selectedNode;
  app.paramFilter[0] = '\0';
  scheduleRender(app);
}

static void clearNodes(App &app) {
  waitRenderIdle(app);
  for (auto &n : app.nodes)
    if (n.instance) callAction(gPlugins[n.pluginIndex].plugin, kOfxActionDestroyInstance, n.instance.get());
  app.nodes.clear();
  app.selectedNode = -1;
  app.paramFilter[0] = '\0';
}

static bool addNode(App &app, int pluginIndex) {
  if (pluginIndex < 0 || pluginIndex >= (int)gPlugins.size()) return false;
  waitRenderIdle(app);
  Node node;
  node.pluginIndex = pluginIndex;
  node.instance = createInstance(gPlugins[pluginIndex]);
  if (!node.instance) {
    app.setStatus("Plugin failed to create an instance");
    return false;
  }
  if (app.preview.w) {
    node.instance->w = app.preview.w;
    node.instance->h = app.preview.h;
  }
  applyColorDefaults(app, node);
  for (auto &p : node.instance->params)
    if (p->type == kOfxParamTypeGroup) node.groupOpen[p->name] = dprop(p->props, kOfxParamPropGroupOpen, 0, 1) != 0;
  app.nodes.push_back(std::move(node));
  app.selectedNode = (int)app.nodes.size() - 1;
  app.paramFilter[0] = '\0';
  syncOutputTag(app);
  scheduleRender(app);
  return true;
}

static void moveNode(App &app, int from, int to) {
  if (from < 0 || to < 0 || from >= (int)app.nodes.size() || to >= (int)app.nodes.size() || from == to) return;
  waitRenderIdle(app);
  Node n = std::move(app.nodes[from]);
  app.nodes.erase(app.nodes.begin() + from);
  app.nodes.insert(app.nodes.begin() + to, std::move(n));
  app.selectedNode = to;
  syncOutputTag(app);
  scheduleRender(app);
}

static void rebuildPreview(App &app) {
  if (app.full.px.empty()) return;
  const int maxEdge = kPreviewRes[std::clamp(app.previewRes, 0, kPreviewResCount - 1)].maxEdge;
  makePreview(app.full, maxEdge, app.preview);
  scheduleRender(app);
}

static void openPath(App &app, const std::string &path) {
  Image img;
  if (!loadImage(path, img)) {
    app.setStatus("Could not decode " + fs::path(path).filename().string());
    return;
  }
  app.path = path;
  app.full = std::move(img);
  app.previewZoom = 1.0f;
  app.previewPan = ImVec2(0, 0);
  app.setStatus("Loaded " + fs::path(path).filename().string());
  rebuildPreview(app);
}

static void uploadTexture(App &app, const Image &img) {
  std::vector<unsigned char> rgba;
  toDisplayRGBA8(img, outputSpace(app.outputIndex), rgba);
  if (!app.tex) glGenTextures(1, &app.tex);
  glBindTexture(GL_TEXTURE_2D, app.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (app.texW != img.w || app.texH != img.h) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    app.texW = img.w;
    app.texH = img.h;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.w, img.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  }
}

static OfxStatus renderChain(App &app, const Image &src, Image &out, int gen) {
  Image cur = src;
  for (size_t i = 0; i < app.nodes.size(); ++i) {
    Node &n = app.nodes[i];
    if (!n.enabled) continue;
    if (!n.instance) return kOfxStatFailed;
    Image next;
    next.w = cur.w;
    next.h = cur.h;
    next.px.resize(cur.px.size());
    const OfxStatus st =
        renderEffect(gPlugins[n.pluginIndex].plugin, n.instance.get(), cur.px.data(), next.px.data(), cur.w, cur.h, gen);
    if (st != kOfxStatOK) return st;
    if (gen != 0 && gen != gLatestGen) return kOfxStatFailed;
    cur = std::move(next);
  }
  out = std::move(cur);
  return kOfxStatOK;
}

static void renderWorker(App *app) {
  while (!app->quit) {
    {
      std::unique_lock<std::mutex> lock(app->renderMutex);
      app->renderCv.wait(lock, [&] { return app->quit || app->renderPending.load(); });
      if (app->quit) break;
      app->renderPending = false;
    }
    if (app->nodes.empty() || app->preview.px.empty()) continue;
    const int gen = ++gLatestGen;
    Image src = app->preview;
    app->setStatus("Rendering...");
    Image out;
    const OfxStatus st = renderChain(*app, src, out, gen);
    if (gen != gLatestGen) continue;
    if (st == kOfxStatOK) {
      std::lock_guard<std::mutex> lock(app->displayMutex);
      app->display = std::move(out);
      app->displayDirty = true;
      app->displayGen = gen;
      app->setStatus(std::to_string(src.w) + "×" + std::to_string(src.h) + " preview");
    } else {
      app->setStatus("Render failed (OFX status " + std::to_string(st) + ")");
    }
  }
}

static bool ancestorsOpen(Node &node, const std::string &group) {
  if (group.empty()) return true;
  Param *g = findParam(node.instance.get(), group.c_str());
  return g && node.groupOpen[group] && ancestorsOpen(node, sprop(g->props, kOfxParamPropParent));
}

static void resetParamValues(Param *p) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (p->type == kOfxParamTypeString || p->type == kOfxParamTypeCustom) {
    p->s = sprop(p->props, kOfxParamPropDefault);
    return;
  }
  for (size_t i = 0; i < p->v.size(); ++i) p->v[i] = dprop(p->props, kOfxParamPropDefault, (int)i, 0);
}

static void drawPencilIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const float pad = (b.x - a.x) * 0.22f;
  const ImVec2 p0(a.x + pad, b.y - pad);
  const ImVec2 p1(b.x - pad, a.y + pad);
  const ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
  const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
  if (len < 1.0f) return;
  const ImVec2 n(-dir.y / len * 1.6f, dir.x / len * 1.6f);
  dl->AddLine(ImVec2(p0.x + n.x, p0.y + n.y), ImVec2(p1.x + n.x, p1.y + n.y), col, 1.2f);
  dl->AddLine(ImVec2(p0.x - n.x, p0.y - n.y), ImVec2(p1.x - n.x, p1.y - n.y), col, 1.2f);
  dl->AddLine(p0, ImVec2(p0.x + dir.x * 0.2f, p0.y + dir.y * 0.2f), col, 1.2f);
  dl->AddLine(ImVec2(p1.x + n.x, p1.y + n.y), ImVec2(p1.x - n.x, p1.y - n.y), col, 1.2f);
}

static void drawResetIcon(ImVec2 a, ImVec2 b) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
  const float r = (b.x - a.x) * 0.28f;
  constexpr float kPi = 3.14159265f;
  dl->PathClear();
  dl->PathArcTo(c, r, kPi * 0.15f, kPi * 1.75f, 16);
  dl->PathStroke(col, 0, 1.4f);
  const float ang = kPi * 0.15f;
  const ImVec2 tip(c.x + std::cos(ang) * r, c.y + std::sin(ang) * r);
  const ImVec2 t1(tip.x - 3.2f, tip.y - 1.2f);
  const ImVec2 t2(tip.x - 1.2f, tip.y + 3.2f);
  dl->AddTriangleFilled(tip, t1, t2, col);
}

static bool paramResetButton() {
  const float h = ImGui::GetFrameHeight();
  const bool clicked = ImGui::Button("##reset", ImVec2(h, h));
  drawResetIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Reset to default");
  return clicked;
}

// Pencil button → type a value. Returns true on commit.
static bool paramEditButton(double &v, bool asInt, double lo, double hi) {
  const float h = ImGui::GetFrameHeight();
  if (ImGui::Button("##edit", ImVec2(h, h))) ImGui::OpenPopup("##type");
  drawPencilIcon(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Type value");

  if (!ImGui::BeginPopup("##type")) return false;
  ImGui::SetKeyboardFocusHere();
  bool commit = false;
  if (asInt) {
    int iv = (int)std::lround(v);
    if (ImGui::InputInt("##v", &iv, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
      v = std::clamp((double)iv, lo, hi);
      commit = true;
    }
  } else if (ImGui::InputDouble("##v", &v, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
    v = std::clamp(v, lo, hi);
    commit = true;
  }
  if (commit) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return commit;
}

static void drawParam(App &app, Param *p) {
  const std::string &t = p->type;
  // OFX labels collide across groups; keep the display label but key ImGui IDs on the unique param name.
  const std::string label = sprop(p->props, kOfxPropLabel);
  const std::string idLabel = (label.empty() ? std::string("param") : label) + "##" + p->name;
  const bool enabled = dprop(p->props, kOfxParamPropEnabled, 0, 1) != 0;
  ImGui::PushID(p->name.c_str());
  if (!enabled) ImGui::BeginDisabled();

  const float btn = ImGui::GetFrameHeight();
  const float gap = ImGui::GetStyle().ItemInnerSpacing.x;

  bool changed = false;
  if (t == kOfxParamTypeDouble || t == kOfxParamTypeInteger) {
    double lo = dprop(p->props, kOfxParamPropDisplayMin, 0, dprop(p->props, kOfxParamPropMin, 0, 0));
    double hi = dprop(p->props, kOfxParamPropDisplayMax, 0, dprop(p->props, kOfxParamPropMax, 0, t == kOfxParamTypeInteger ? 100 : 1));
    if (!(std::fabs(lo) < 1e7)) lo = 0;
    if (!(std::fabs(hi) < 1e7) || hi <= lo) hi = lo + (t == kOfxParamTypeInteger ? 100 : 1);
    const double hardLo = dprop(p->props, kOfxParamPropMin, 0, lo);
    const double hardHi = dprop(p->props, kOfxParamPropMax, 0, hi);
    const float rowW = ImGui::CalcItemWidth();

    if (paramResetButton()) {
      resetParamValues(p);
      changed = true;
    }
    ImGui::SameLine(0, gap);
    double typed = p->v[0];
    if (paramEditButton(typed, t == kOfxParamTypeInteger, hardLo, hardHi)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = typed;
      changed = true;
    }
    ImGui::SameLine(0, gap);
    ImGui::SetNextItemWidth(std::max(40.0f, rowW - 2 * btn - 2 * gap));
    float fv = (float)p->v[0];
    if (ImGui::SliderFloat(idLabel.c_str(), &fv, (float)lo, (float)hi)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = t == kOfxParamTypeInteger ? std::round(fv) : fv;
      changed = true;
    }
  } else if (t == kOfxParamTypeBoolean) {
    if (paramResetButton()) {
      resetParamValues(p);
      changed = true;
    }
    ImGui::SameLine(0, gap);
    bool v = p->v[0] != 0;
    if (ImGui::Checkbox(idLabel.c_str(), &v)) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = v ? 1 : 0;
      changed = true;
    }
  } else if (t == kOfxParamTypeChoice) {
    if (paramResetButton()) {
      resetParamValues(p);
      changed = true;
    }
    ImGui::SameLine(0, gap);
    const auto &opts = choiceOptions(p);
    int cur = (int)p->v[0];
    std::vector<const char *> items;
    items.reserve(opts.size());
    for (auto &o : opts) items.push_back(o.s.c_str());
    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - btn - gap));
    if (!items.empty() && ImGui::Combo(idLabel.c_str(), &cur, items.data(), (int)items.size())) {
      std::lock_guard<std::mutex> lock(gValueMutex);
      p->v[0] = cur;
      changed = true;
    }
  } else if (t == kOfxParamTypePushButton) {
    if (ImGui::Button(idLabel.c_str())) changed = true;
  } else if (t == kOfxParamTypeString) {
    char buf[512];
    std::snprintf(buf, sizeof buf, "%s", p->s.c_str());
    const bool editable = sprop(p->props, kOfxParamPropStringMode) != kOfxParamStringIsLabel;
    if (editable) {
      if (paramResetButton()) {
        resetParamValues(p);
        changed = true;
      }
      ImGui::SameLine(0, gap);
      ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - btn - gap));
      if (ImGui::InputText(idLabel.c_str(), buf, sizeof buf)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->s = buf;
        changed = true;
      }
    } else {
      ImGui::Text("%s: %s", label.c_str(), p->s.c_str());
    }
  } else if (dims(t) > 1) {
    if (paramResetButton()) {
      resetParamValues(p);
      changed = true;
    }
    ImGui::SameLine(0, gap);
    ImGui::TextUnformatted(label.c_str());
    ImGui::Indent();
    for (int i = 0; i < dims(t); ++i) {
      float fv = (float)p->v[i];
      ImGui::PushID(i);
      const float rowW = ImGui::CalcItemWidth();
      double typed = p->v[i];
      if (paramEditButton(typed, isIntType(t), -1e7, 1e7)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[i] = typed;
        changed = true;
      }
      ImGui::SameLine(0, gap);
      ImGui::SetNextItemWidth(std::max(40.0f, rowW - btn - gap));
      if (ImGui::DragFloat("##v", &fv, isIntType(t) ? 1.0f : 0.01f)) {
        std::lock_guard<std::mutex> lock(gValueMutex);
        p->v[i] = isIntType(t) ? std::round(fv) : fv;
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::Unindent();
  }

  if (!enabled) ImGui::EndDisabled();
  ImGui::PopID();
  if (changed) {
    Node *node = selectedNode(app);
    if (node) {
      notifyChanged(app, *node, p);
      syncOutputTag(app);
      scheduleRender(app);
    }
  }
}

static bool icontains(const std::string &hay, const std::string &needle) {
  if (needle.empty()) return true;
  auto lower = [](unsigned char c) { return (char)std::tolower(c); };
  auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                        [&](char a, char b) { return lower(a) == lower((unsigned char)b); });
  return it != hay.end();
}

static bool paramMatches(Param *p, const std::string &q) {
  return icontains(sprop(p->props, kOfxPropLabel), q) || icontains(p->name, q) ||
         icontains(sprop(p->props, kOfxParamPropHint), q);
}

static bool subtreeMatches(Effect *e, const std::string &parent, const std::string &q) {
  for (auto &up : e->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    if (p->type == kOfxParamTypeGroup) {
      if (paramMatches(p, q) || subtreeMatches(e, p->name, q)) return true;
    } else if (paramMatches(p, q)) {
      return true;
    }
  }
  return false;
}

static void drawParams(App &app, Node &node, const std::string &parent) {
  const std::string filter = app.paramFilter;
  const bool filtering = filter[0] != '\0';
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (sprop(p->props, kOfxParamPropParent) != parent || p->type == kOfxParamTypePage) continue;
    if (dprop(p->props, kOfxParamPropSecret, 0, 0) != 0) continue;
    if (p->type == kOfxParamTypeGroup) {
      if (filtering && !subtreeMatches(node.instance.get(), p->name, filter) && !paramMatches(p, filter)) continue;
      if (!filtering && !ancestorsOpen(node, parent) && parent != "") continue;
      const std::string groupLabel = sprop(p->props, kOfxPropLabel) + "##" + p->name;
      if (filtering) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
      else ImGui::SetNextItemOpen(node.groupOpen[p->name], ImGuiCond_Once);
      if (ImGui::CollapsingHeader(groupLabel.c_str())) {
        if (!filtering) node.groupOpen[p->name] = true;
        ImGui::Indent();
        drawParams(app, node, p->name);
        ImGui::Unindent();
      } else if (!filtering) {
        node.groupOpen[p->name] = false;
      }
    } else {
      if (filtering) {
        if (!paramMatches(p, filter)) continue;
      } else if (!ancestorsOpen(node, parent)) {
        continue;
      }
      drawParam(app, p);
    }
  }
}

static void vSplitter(const char *id, float *size, float minSize, float maxSize, float sign) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
  ImGui::Button(id, ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
  if (ImGui::IsItemActive()) {
    *size += sign * ImGui::GetIO().MouseDelta.x;
    *size = std::clamp(*size, minSize, maxSize);
  }
  if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  ImGui::PopStyleColor(3);
}

static ImWchar utf8Codepoint(const char *s) {
  const unsigned char *u = (const unsigned char *)s;
  if (u[0] < 0x80) return (ImWchar)u[0];
  if ((u[0] & 0xE0) == 0xC0) return (ImWchar)(((u[0] & 0x1F) << 6) | (u[1] & 0x3F));
  if ((u[0] & 0xF0) == 0xE0) return (ImWchar)(((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F));
  return 0;
}

static bool iconBtn(const char *id, const char *icon) {
  const float h = ImGui::GetFrameHeight();
  ImGui::PushID(id);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton("##", ImVec2(h, h));
  const ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive()   ? ImGuiCol_ButtonActive
                                      : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered
                                                               : ImGuiCol_Button);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + h, p.y + h), bg, ImGui::GetStyle().FrameRounding);

  // Center on glyph ink bounds (CalcTextSize line metrics don't match FA visual boxes).
  ImFont *font = ImGui::GetFont();
  const float fontSize = ImGui::GetFontSize();
  const ImWchar cp = utf8Codepoint(icon);
  if (const ImFontGlyph *g = font->FindGlyph(cp)) {
    const float scale = fontSize / font->FontSize;
    const float x = p.x + h * 0.5f - (g->X0 + g->X1) * scale * 0.5f;
    const float y = p.y + h * 0.5f - (g->Y0 + g->Y1) * scale * 0.5f;
    font->RenderChar(dl, fontSize, ImVec2(std::floor(x), std::floor(y)), ImGui::GetColorU32(ImGuiCol_Text), cp);
  }
  ImGui::PopID();
  return pressed;
}

static void doExport(App &app) {
  if (app.full.px.empty() || app.nodes.empty()) return;
  const char *exts[] = {".png", ".jpg"};
  const char *filters[] = {"PNG (8-bit)", "*.png", "JPEG", "*.jpg *.jpeg"};
  std::string def = fs::path(app.path).stem().string() + exts[app.exportFormat];
  auto sel = pfd::save_file("Export", def, {filters[app.exportFormat * 2], filters[app.exportFormat * 2 + 1]});
  std::string outPath = sel.result();
  if (outPath.empty()) return;
  if (fs::path(outPath).extension().empty()) outPath += exts[app.exportFormat];

  app.setStatus("Exporting full resolution...");
  waitRenderIdle(app);
  const int pw = app.preview.w, ph = app.preview.h;
  Image src = app.full;
  const ColorSpace space = outputSpace(app.outputIndex);
  const int jpegQuality = app.jpegQuality;
  std::thread([&, src, outPath, pw, ph, space, jpegQuality]() mutable {
    for (auto &n : app.nodes)
      if (n.instance) {
        n.instance->w = src.w;
        n.instance->h = src.h;
      }
    Image out;
    OfxStatus st = renderChain(app, src, out, 0);
    for (auto &n : app.nodes)
      if (n.instance) {
        n.instance->w = pw;
        n.instance->h = ph;
      }
    bool ok = st == kOfxStatOK && writeImage(out, outPath, space, jpegQuality);
    app.setStatus(ok ? "Exported " + fs::path(outPath).filename().string() + " (" + std::to_string(src.w) + "×" +
                            std::to_string(src.h) + ")"
                      : "Export failed (OFX status " + std::to_string(st) + ")");
  }).detach();
}

int runApp(const std::string &optionalPath) {
  if (!glfwInit()) return 1;
#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

  App app;
  app.window = glfwCreateWindow(1400, 900, "OFX Raw Host", nullptr, nullptr);
  if (!app.window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(app.window);
  glfwSwapInterval(1);
#if defined(__APPLE__)
  MacPinch_Install();
#endif

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  // Rasterize fonts at framebuffer DPI so Retina text stays sharp (GLFW already sets DisplayFramebufferScale).
  float dpiX = 1.0f, dpiY = 1.0f;
  glfwGetWindowContentScale(app.window, &dpiX, &dpiY);
  const float dpi = std::max(1.0f, std::max(dpiX, dpiY));
  ImFontConfig fontCfg;
  fontCfg.SizePixels = std::round(13.0f * dpi);
  fontCfg.OversampleH = 2;
  fontCfg.OversampleV = 2;
  io.Fonts->Clear();
  io.Fonts->AddFontDefault(&fontCfg);
  {
    // Merge Font Awesome solid icons into the default font (imgui docs/FONTS.md).
    ImFontConfig iconsCfg;
    iconsCfg.MergeMode = true;
    iconsCfg.PixelSnapH = true;
    iconsCfg.GlyphMinAdvanceX = fontCfg.SizePixels;
    iconsCfg.OversampleH = 2;
    iconsCfg.OversampleV = 2;
    // Only the glyphs we use — full ICON_MIN/MAX_FA would bloat the atlas.
    static const ImWchar iconRanges[] = {0xf00d, 0xf00d, 0xf053, 0xf055, 0xf062, 0xf063, 0xf06e, 0xf070, 0};
    const char *cands[] = {
        OFX_ICON_FONT_PATH,
        "fa-solid-900.ttf",
        "../Resources/fa-solid-900.ttf",
    };
    bool loaded = false;
    for (const char *path : cands) {
      if (!path || !path[0] || !fs::exists(path)) continue;
      if (io.Fonts->AddFontFromFileTTF(path, fontCfg.SizePixels, &iconsCfg, iconRanges)) {
        loaded = true;
        break;
      }
    }
    if (!loaded) std::fprintf(stderr, "warning: could not load Font Awesome icon font\n");
  }
  io.FontGlobalScale = 1.0f / dpi;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(app.window, true);
#if defined(__APPLE__)
  ImGui_ImplOpenGL3_Init("#version 150");
#else
  ImGui_ImplOpenGL3_Init("#version 330");
#endif

  gOnMessage = [&app](const std::string &msg) { app.setStatus(msg); };
  loadPlugins();
  if (gPlugins.empty())
    app.setStatus("No OFX filter plugins found in the default OFX path or OFX_PLUGIN_PATH");
  else
    app.setStatus("Add plugins with + to build a processing chain.");
  if (!optionalPath.empty()) openPath(app, optionalPath);

  app.renderThread = std::thread(renderWorker, &app);

  glfwSetDropCallback(app.window, [](GLFWwindow *w, int count, const char **paths) {
    auto *app = static_cast<App *>(glfwGetWindowUserPointer(w));
    if (app && count > 0) openPath(*app, paths[0]);
  });
  glfwSetWindowUserPointer(app.window, &app);

  while (!glfwWindowShouldClose(app.window)) {
    glfwPollEvents();

    {
      std::lock_guard<std::mutex> lock(app.displayMutex);
      if (app.displayDirty && !app.display.px.empty()) {
        uploadTexture(app, app.display);
        app.displayDirty = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
          auto f = pfd::open_file("Open image", "", {"Images", "*.*"});
          auto r = f.result();
          if (!r.empty()) openPath(app, r[0]);
        }
        if (ImGui::MenuItem("Export...", "Ctrl+E")) doExport(app);
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) glfwSetWindowShouldClose(app.window, 1);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Left panel", "Ctrl+[", &app.showLeft);
        ImGui::MenuItem("Right panel", "Ctrl+]", &app.showRight);
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_LeftBracket) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_LeftBracket))
      app.showLeft = !app.showLeft;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_RightBracket) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_RightBracket))
      app.showRight = !app.showRight;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float splitW = 4.0f;
    const float minPanel = 180.0f;
    const float minPreview = 160.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float splits = (app.showLeft ? splitW : 0.0f) + (app.showRight ? splitW : 0.0f);
    const float maxLeft = app.showLeft ? std::max(minPanel, avail - splits - minPreview - (app.showRight ? app.rightW : 0.0f)) : minPanel;
    const float maxRight = app.showRight ? std::max(minPanel, avail - splits - minPreview - (app.showLeft ? app.leftW : 0.0f)) : minPanel;
    if (app.showLeft) app.leftW = std::clamp(app.leftW, minPanel, maxLeft);
    if (app.showRight) app.rightW = std::clamp(app.rightW, minPanel, maxRight);
    const float previewW = avail - splits - (app.showLeft ? app.leftW : 0.0f) - (app.showRight ? app.rightW : 0.0f);

    if (app.showLeft) {
      ImGui::BeginChild("left", ImVec2(app.leftW, 0), true);
      if (iconBtn("##hideLeft", ICON_FA_CHEVRON_LEFT)) app.showLeft = false;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Hide left panel (Ctrl+[)");
      ImGui::SameLine();
      if (ImGui::Button("Open...")) {
        auto f = pfd::open_file("Open image", "", {"Images", "*.*"});
        auto r = f.result();
        if (!r.empty()) openPath(app, r[0]);
      }
      ImGui::SameLine();
      if (ImGui::Button("Export...")) doExport(app);

      ImGui::Combo("Output tag", &app.outputIndex, kOutputSpaces, 4);
      if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited()) {
        std::lock_guard<std::mutex> lock(app.displayMutex);
        if (!app.display.px.empty()) app.displayDirty = true;
      }
      {
        const char *items[kPreviewResCount];
        for (int i = 0; i < kPreviewResCount; ++i) items[i] = kPreviewRes[i].label;
        if (ImGui::Combo("Preview", &app.previewRes, items, kPreviewResCount)) rebuildPreview(app);
      }
      ImGui::Combo("Export format", &app.exportFormat, "PNG (8-bit)\0JPEG\0");
      if (app.exportFormat == 1) ImGui::SliderInt("JPEG quality", &app.jpegQuality, 1, 100);
      ImGui::Separator();
      const std::string status = app.getStatus();
      ImGui::TextWrapped("%s", status.c_str());
      ImGui::Separator();

      ImGui::TextUnformatted("OFX Plugin Nodes");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##addPlugin", "Add plugin…", ImGuiComboFlags_HeightLargest)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##pluginFilter", "Search…", app.pluginFilter, sizeof app.pluginFilter);
        ImGui::Separator();
        if (gPlugins.empty()) {
          ImGui::TextDisabled("No plugins found");
        } else {
          const std::string q = app.pluginFilter;
          std::string curAuthor;
          int shown = 0;
          for (int i = 0; i < (int)gPlugins.size(); ++i) {
            const auto &pe = gPlugins[i];
            if (!q.empty() && !icontains(pe.label, q) && !icontains(pe.author, q) &&
                !(pe.plugin && pe.plugin->pluginIdentifier && icontains(pe.plugin->pluginIdentifier, q)))
              continue;
            if (pe.author != curAuthor) {
              curAuthor = pe.author;
              ImGui::SeparatorText(curAuthor.c_str());
            }
            if (ImGui::Selectable(pe.label.c_str())) {
              addNode(app, i);
              app.pluginFilter[0] = '\0';
              ImGui::CloseCurrentPopup();
            }
            ++shown;
          }
          if (shown == 0) ImGui::TextDisabled("No matches");
        }
        ImGui::EndCombo();
      }

      ImGui::BeginChild("nodeList", ImVec2(0, 0), true);
      if (app.nodes.empty()) {
        ImGui::TextDisabled("No nodes yet.\nPress + to add a plugin.");
      }
      const float btnH = ImGui::GetFrameHeight();
      const float btnGap = ImGui::GetStyle().ItemSpacing.x;
      const float btnsW = 4.0f * btnH + 3.0f * btnGap;
      int removeAt = -1;
      for (int i = 0; i < (int)app.nodes.size(); ++i) {
        ImGui::PushID(i);
        Node &node = app.nodes[i];
        const bool selected = app.selectedNode == i;
        const std::string label = gPlugins[node.pluginIndex].label;
        if (!node.enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
        if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(ImGui::GetContentRegionAvail().x - btnsW - btnGap, btnH))) {
          app.selectedNode = i;
          app.paramFilter[0] = '\0';
        }
        if (!node.enabled) ImGui::PopStyleVar();
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload("NODE_IDX", &i, sizeof(i));
          ImGui::Text("%s", label.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("NODE_IDX")) {
            const int from = *(const int *)payload->Data;
            moveNode(app, from, i);
          }
          ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0.0f, btnGap);
        if (iconBtn("##en", node.enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
          node.enabled = !node.enabled;
          scheduleRender(app);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
          ImGui::SetTooltip(node.enabled ? "Disable processing" : "Enable processing");
        ImGui::SameLine(0.0f, btnGap);
        if (iconBtn("##up", ICON_FA_ARROW_UP) && i > 0) moveNode(app, i, i - 1);
        ImGui::SameLine(0.0f, btnGap);
        if (iconBtn("##dn", ICON_FA_ARROW_DOWN) && i + 1 < (int)app.nodes.size()) moveNode(app, i, i + 1);
        ImGui::SameLine(0.0f, btnGap);
        if (iconBtn("##rm", ICON_FA_XMARK)) removeAt = i;
        ImGui::PopID();
      }
      if (removeAt >= 0) destroyNode(app, removeAt);
      ImGui::EndChild();
      ImGui::EndChild();

      ImGui::SameLine(0.0f, 0.0f);
      vSplitter("##splitL", &app.leftW, minPanel, maxLeft, +1.0f);
      ImGui::SameLine(0.0f, 0.0f);
    }

    ImGui::BeginChild("preview", ImVec2(std::max(minPreview, previewW), 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
      const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
      ImGui::InvisibleButton("##previewCanvas", canvasSize);
      const bool hovered = ImGui::IsItemHovered();
#if !defined(__APPLE__)
      const bool active = ImGui::IsItemActive();
#endif
#if defined(__APPLE__)
      const float pinch = MacPinch_Consume();
#else
      const float pinch = 0.0f;
#endif

      if (app.tex) {
        const float fit = std::min(canvasSize.x / (float)app.texW, canvasSize.y / (float)app.texH);
        if (hovered) {
          float zoomFactor = 1.0f;
#if defined(__APPLE__)
          // macOS: pinch = zoom, two-finger scroll = pan (no wheel-zoom / drag-pan)
          // GLFW multiplies precise trackpad deltas by 0.1 → scale 10 ≈ 1:1 screen points.
          if (pinch != 0.0f) {
            zoomFactor *= (1.0f + pinch);
          } else {
            constexpr float kPanScale = 10.0f;
            app.previewPan.x += ImGui::GetIO().MouseWheelH * kPanScale;
            app.previewPan.y += ImGui::GetIO().MouseWheel * kPanScale;
          }
#else
          const float wheel = ImGui::GetIO().MouseWheel;
          if (wheel != 0.0f) zoomFactor *= (wheel > 0.0f ? 1.1f : 1.0f / 1.1f);
#endif
          if (zoomFactor != 1.0f) {
            const float oldZoom = app.previewZoom;
            app.previewZoom = std::clamp(app.previewZoom * zoomFactor, 0.05f, 64.0f);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float ox = canvasPos.x + canvasSize.x * 0.5f + app.previewPan.x;
            const float oy = canvasPos.y + canvasSize.y * 0.5f + app.previewPan.y;
            const float oldW = app.texW * fit * oldZoom, oldH = app.texH * fit * oldZoom;
            const float newW = app.texW * fit * app.previewZoom, newH = app.texH * fit * app.previewZoom;
            const float u = oldW > 0.0f ? (mouse.x - (ox - oldW * 0.5f)) / oldW : 0.5f;
            const float v = oldH > 0.0f ? (mouse.y - (oy - oldH * 0.5f)) / oldH : 0.5f;
            app.previewPan.x += (u - 0.5f) * (oldW - newW);
            app.previewPan.y += (v - 0.5f) * (oldH - newH);
          }
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            app.previewZoom = 1.0f;
            app.previewPan = ImVec2(0, 0);
          }
        }
#if !defined(__APPLE__)
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          app.previewPan.x += ImGui::GetIO().MouseDelta.x;
          app.previewPan.y += ImGui::GetIO().MouseDelta.y;
        }
#endif

        const float dispW = app.texW * fit * app.previewZoom;
        const float dispH = app.texH * fit * app.previewZoom;
        const ImVec2 p0(canvasPos.x + canvasSize.x * 0.5f + app.previewPan.x - dispW * 0.5f,
                        canvasPos.y + canvasSize.y * 0.5f + app.previewPan.y - dispH * 0.5f);
        const ImVec2 p1(p0.x + dispW, p0.y + dispH);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);
        dl->AddImage((ImTextureID)(intptr_t)app.tex, p0, p1);
        dl->PopClipRect();

        char zoomLbl[32];
        std::snprintf(zoomLbl, sizeof zoomLbl, "%.0f%%", app.previewZoom * 100.0f);
        dl->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + canvasSize.y - ImGui::GetTextLineHeight() - 8.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), zoomLbl);
      } else {
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f));
        ImGui::TextUnformatted("Open an image to preview.");
      }
    }
    if (!app.showLeft) {
      ImGui::SetCursorPos(ImVec2(8.0f, 8.0f));
      if (iconBtn("##showLeft", ICON_FA_CHEVRON_RIGHT)) app.showLeft = true;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Show left panel (Ctrl+[)");
    }
    if (!app.showRight) {
      ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight() - 8.0f, 8.0f));
      if (iconBtn("##showRight", ICON_FA_CHEVRON_LEFT)) app.showRight = true;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Show right panel (Ctrl+])");
    }
    ImGui::EndChild();

    if (app.showRight) {
      ImGui::SameLine(0.0f, 0.0f);
      vSplitter("##splitR", &app.rightW, minPanel, maxRight, -1.0f);
      ImGui::SameLine(0.0f, 0.0f);
      ImGui::BeginChild("right", ImVec2(app.rightW, 0), true);
      if (iconBtn("##hideRight", ICON_FA_CHEVRON_RIGHT)) app.showRight = false;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Hide right panel (Ctrl+])");
      Node *node = selectedNode(app);
      if (!node) {
        ImGui::TextDisabled("Select a node to edit parameters.");
      } else {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(gPlugins[node->pluginIndex].label.c_str());
        ImGui::Separator();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F) || ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_F))
          ImGui::SetKeyboardFocusHere();
        const bool hasFilter = app.paramFilter[0] != '\0';
        if (hasFilter) {
          const float clearW = ImGui::GetFrameHeight();
          ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearW - ImGui::GetStyle().ItemSpacing.x);
        } else {
          ImGui::SetNextItemWidth(-1);
        }
        ImGui::InputTextWithHint("##paramFilter", "Search parameters...", app.paramFilter, sizeof app.paramFilter);
        if (hasFilter) {
          ImGui::SameLine();
          if (iconBtn("##clearFilter", ICON_FA_XMARK)) app.paramFilter[0] = '\0';
        }
        ImGui::Separator();
        drawParams(app, *node, "");
      }
      ImGui::EndChild();
    }

    ImGui::End();

    ImGui::Render();
    int dw, dh;
    glfwGetFramebufferSize(app.window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(app.window);
  }

  app.quit = true;
  app.renderCv.notify_one();
  if (app.renderThread.joinable()) app.renderThread.join();
  clearNodes(app);

  if (app.tex) glDeleteTextures(1, &app.tex);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
#if defined(__APPLE__)
  MacPinch_Shutdown();
#endif
  glfwDestroyWindow(app.window);
  glfwTerminate();
  return 0;
}
