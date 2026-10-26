#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "services/Actions.h"
#include "app/AppState.h"
#include "services/ChainIO.h"
#include "services/NodeGraph.h"
#include "services/StateMapping.h"
#include "services/UpdateCheck.h"
#include "codecs/ImageIO.h"
#include "ofx/OfxHost.h"
#include "persist/ProjectPersist.h"
#include "persist/ProjectPersistPriv.h"
#include "platform/EmbeddedResource.h"
#include "platform/Paths.h"
#include "platform/SystemUiFont.h"
#include "ui/Themes.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// The release bundles the interface font (Inter) and the icon font, so the host
// never depends on a system font. The interface font must also be loadable: a
// truncated or unsupported file would fail here instead of at startup.
int testBundledFonts() {
#if defined(_WIN32)
  if (embeddedResource(kIconFontResourceId).empty()) return fail("embedded icon font is missing");
  if (embeddedResource(kUiFontResourceId).empty()) return fail("embedded interface font is missing");
#endif

  // Read the interface font from the same places the UI does.
  std::vector<unsigned char> bytes = embeddedResource(kUiFontResourceId);
  if (bytes.empty()) {
    const fs::path exe = exeDir();
    const std::vector<std::string> cands = {
        OFX_UI_FONT_PATH,
        (exe / "Inter.ttf").string(),
        (exe / ".." / "Resources" / "Inter.ttf").string(),
        "Inter.ttf",
    };
    for (const std::string &path : cands) {
      if (path.empty() || !fs::exists(path)) continue;
      std::ifstream in(path, std::ios::binary);
      if (!in) continue;
      bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      if (!bytes.empty()) break;
    }
  }
  if (bytes.empty()) return fail("bundled interface font not found");

  ImFontAtlas atlas;
  ImFontConfig cfg;
  cfg.SizePixels = 18.0f;
  // ImGui takes ownership of the buffer, so hand it a heap copy.
  void *copy = std::malloc(bytes.size());
  if (!copy) return fail("bundled interface font: out of memory");
  std::memcpy(copy, bytes.data(), bytes.size());
  ImFont *font = atlas.AddFontFromMemoryTTF(copy, (int)bytes.size(), cfg.SizePixels, &cfg);
  if (!font) return fail("bundled interface font did not load");
  if (!atlas.Build()) return fail("bundled interface font atlas did not build");
  unsigned char *pixels = nullptr;
  int texW = 0, texH = 0;
  atlas.GetTexDataAsAlpha8(&pixels, &texW, &texH);
  if (!pixels || texW <= 0 || texH <= 0) return fail("bundled interface font has no atlas texture");
  if (!font->FindGlyphNoFallback('A')) return fail("bundled interface font has no glyphs");

  // The interface font size follows the operating system, so it must resolve to
  // a usable size on every platform and in a headless run.
  const float systemPt = systemUiFontSizePt();
  if (!(systemPt > 4.0f) || systemPt > 40.0f) return fail("system UI font size is out of range");

  printf("ok  bundled fonts (system size %.1f pt)\n", systemPt);
  return 0;
}

// The font setting is a point size, and 0 follows the system size. It belongs to
// the settings file, so a settings file written while the setting was a scale of
// the system size must still apply, and a hand-edited value must snap back into
// the range.
int testFontSizeMigration() {
  const float systemPt = systemUiFontSizePt();

  PersistAppearance appearance;
  JsonValue none = JsonValue::makeObject();
  loadAppearance(none, appearance);
  if (appearance.uiFontPt != kUiFontPtAuto)
    return fail("font size: a file without the setting does not follow the system");

  JsonValue explicitPt = JsonValue::makeObject();
  explicitPt.set("uiFontSizePt", JsonValue::makeInt(17));
  loadAppearance(explicitPt, appearance);
  if (appearance.uiFontPt != 17) return fail("font size: the stored point size was not read");

  // Scale 1 was the system size, so it becomes the automatic setting.
  JsonValue normal = JsonValue::makeObject();
  normal.set("uiFontScale", JsonValue::makeInt(1));
  loadAppearance(normal, appearance);
  if (appearance.uiFontPt != kUiFontPtAuto) return fail("font size: the normal scale did not follow the system");

  // Scales 0 and 2 were 0.85 and 1.2 of the system size.
  JsonValue smaller = JsonValue::makeObject();
  smaller.set("uiFontScale", JsonValue::makeInt(0));
  loadAppearance(smaller, appearance);
  if (appearance.uiFontPt != (int)std::lround(systemPt * 0.85f))
    return fail("font size: the smaller scale did not convert");

  JsonValue bigger = JsonValue::makeObject();
  bigger.set("uiFontScale", JsonValue::makeInt(2));
  loadAppearance(bigger, appearance);
  if (appearance.uiFontPt != (int)std::lround(systemPt * 1.2f))
    return fail("font size: the bigger scale did not convert");

  App app;
  PersistAppearance tooSmall;
  tooSmall.uiFontPt = 2;
  applyAppearance(app.gui, tooSmall);
  if (app.gui.uiFontPt != kUiFontPtMin) return fail("font size: a small value was not raised to the minimum");
  PersistAppearance tooBig;
  tooBig.uiFontPt = 200;
  applyAppearance(app.gui, tooBig);
  if (app.gui.uiFontPt != kUiFontPtMax) return fail("font size: a large value was not lowered to the maximum");
  PersistAppearance negative;
  negative.uiFontPt = -5;
  applyAppearance(app.gui, negative);
  if (app.gui.uiFontPt != kUiFontPtAuto) return fail("font size: a negative value did not return to the system size");

  printf("ok  font size migration (system size %.1f pt)\n", systemPt);
  return 0;
}

// The state of a file or a directory lives in the per-user state directory, one
// entry per image, per export, and per directory, and none of it is written
// beside the source. The interface state belongs to the user alone, so these
// files carry none of it. A file that a build before the move wrote beside the
// source is still read while the state file is missing.
int testStateFilesAreAppLevel() {
  const fs::path base = fs::temp_directory_path() / "ofxrawhost-selftest-state";
  const fs::path dir = base / "shots";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(dir, ec);

  // Point the configuration directory at the temp folder, so the state files
  // land there instead of the real user directory.
#ifdef _WIN32
  const char *envKey = "APPDATA";
#else
  const char *envKey = "HOME";
#endif
  const char *oldEnv = std::getenv(envKey);
  std::string oldValue;
  if (oldEnv) oldValue = oldEnv;
#ifdef _WIN32
  _putenv_s(envKey, base.string().c_str());
#else
  setenv(envKey, base.string().c_str(), 1);
#endif

  int rc = 0;
  const std::string image = (dir / "shot.png").string();
  const std::string out = (dir / "shot-export.jpg").string();
  PersistGui gui;

  PersistChain chain;
  chain.selectedNode = 0;
  PersistNode node;
  node.pluginIdentifier = "com.example.chain";
  node.pluginLabel = "Chain";
  // One value of each shape a plugin parameter can take, so the sidecar proves
  // the JSON round-trip rather than one number.
  node.params["title"] = JsonValue::makeString("grain \"fine\"");
  node.params["enabled"] = JsonValue::makeBool(true);
  node.params["amount"] = JsonValue::makeDouble(0.35);
  JsonValue center = JsonValue::makeArray();
  center.arr.push_back(JsonValue::makeDouble(0.25));
  center.arr.push_back(JsonValue::makeDouble(0.75));
  node.params["center"] = center;
  chain.nodes.push_back(node);

  // A save writes under the state directory. The state of a directory is an entry
  // inside the mirror of that directory, and nothing lands beside the source.
  if (rc == 0 && !saveWorkspaceProject(dir.string(), std::string("shot.png")))
    rc = fail("state: the project was not written");
  if (rc == 0 && !saveInputSidecar(image, ColorSpace::sRGB, ColorSpace::sRGB, chain))
    rc = fail("state: the sidecar was not written");
  if (rc == 0 && !saveExportSidecar(out, image, ColorSpace::sRGB, chain))
    rc = fail("state: the export sidecar was not written");
  if (rc == 0 && fs::exists(dir / "workspace.ofxrawhost.json"))
    rc = fail("state: a project was written beside the directory");
  if (rc == 0 && fs::exists(image + ".ofxrawhost.json"))
    rc = fail("state: a sidecar was written beside the image");
  if (rc == 0 && fs::exists(dir / "shot-export.json"))
    rc = fail("state: a sidecar was written beside the export");

  const fs::path state = userStateDir();
  if (rc == 0 && state.empty()) rc = fail("state: the state directory was not found");
  if (rc == 0) {
    const std::string project = workspaceProjectPath(dir.string());
    const std::string sidecar = inputSidecarPath(image);
    const std::string exported = exportSidecarPath(out);
    if (project.rfind(state.string(), 0) != 0 || sidecar.rfind(state.string(), 0) != 0 ||
        exported.rfind(state.string(), 0) != 0)
      rc = fail("state: a state file was not written under the state directory");
  }

  // The parameter values come back with their type and their text intact.
  if (rc == 0) {
    PersistSidecar loaded;
    if (!loadInputSidecar(image, loaded) || loaded.chain.nodes.size() != 1) {
      rc = fail("state: the sidecar chain was not read back");
    } else {
      const PersistNode &back = loaded.chain.nodes[0];
      if (back.params.size() != node.params.size())
        rc = fail("state: the sidecar lost or gained parameters");
      static const char *const kKeys[] = {"title", "enabled", "amount", "center"};
      for (const char *key : kKeys) {
        if (rc != 0) break;
        auto original = node.params.find(key);
        auto restored = back.params.find(key);
        if (restored == back.params.end() || restored->second.dump() != original->second.dump())
          rc = fail((std::string("state: parameter ") + key + " did not round-trip").c_str());
      }
    }
  }

  // None of the three carries interface state, so opening one cannot change the
  // view, the color spaces, the panels, the theme, or the font.
  if (rc == 0) {
    static const char *const kKeys[] = {"gui",       "showPlugins",  "showParameters", "showFilmstrip",
                                        "previewRes", "themeIndex",   "uiFontSizePt",   "exportQuality"};
    const char *files[] = {"the project", "the sidecar", "the export sidecar"};
    const std::string paths[] = {workspaceProjectPath(dir.string()), inputSidecarPath(image),
                                 exportSidecarPath(out)};
    for (int i = 0; i < 3 && rc == 0; ++i) {
      std::string text;
      if (!readAllText(paths[i], text)) {
        rc = fail("state: a state file was not read");
        break;
      }
      for (const char *key : kKeys) {
        if (text.find(key) != std::string::npos) {
          rc = fail((std::string("state: ") + files[i] + " carries " + key).c_str());
          break;
        }
      }
    }
  }
  // A sidecar and a project written before the move sit beside the source. They
  // must still be read while the state directory holds nothing for that file.
  if (rc == 0) {
    fs::remove(workspaceProjectPath(dir.string()), ec);
    fs::remove(inputSidecarPath(image), ec);
    static const char *kLegacySidecar =
        "{\"format\":\"ofxrawhost-sidecar\",\"version\":1,\"kind\":\"input\","
        "\"inputColorSpace\":\"sRGB\",\"chainInputColorSpace\":\"sRGB\","
        "\"chain\":{\"selectedNode\":0,\"nodes\":[{\"pluginIdentifier\":\"com.example.legacy\","
        "\"pluginLabel\":\"Legacy\",\"enabled\":true,\"groupOpen\":{},\"params\":{}}]}}";
    if (!writeFile(inputSidecarPathLegacy(image), kLegacySidecar))
      rc = fail("state: the legacy sidecar was not written");
  }
  if (rc == 0) {
    PersistSidecar legacy;
    if (!loadInputSidecar(image, legacy) || legacy.chain.nodes.size() != 1 ||
        legacy.chain.nodes[0].pluginIdentifier != "com.example.legacy")
      rc = fail("state: the sidecar beside the image was not read");
  }
  if (rc == 0) {
    JsonValue legacyProject = JsonValue::makeObject();
    legacyProject.set("format", JsonValue::makeString("ofxrawhost-workspace"));
    legacyProject.set("version", JsonValue::makeInt(1));
    legacyProject.set("activeImage", JsonValue::makeString("shot.png"));
    if (!writeFile(workspaceProjectPathLegacy(dir.string()), legacyProject.dump()))
      rc = fail("state: the legacy project was not written");
  }
  if (rc == 0) {
    std::string activeRel;
    if (!loadWorkspaceProject(dir.string(), activeRel) || activeRel != "shot.png")
      rc = fail("state: the project beside the directory was not read");
  }

  if (rc == 0) std::printf("ok  state files are app level\n");

#ifdef _WIN32
  if (oldValue.empty()) _putenv_s(envKey, "");
  else _putenv_s(envKey, oldValue.c_str());
#else
  if (oldValue.empty()) unsetenv(envKey);
  else setenv(envKey, oldValue.c_str(), 1);
#endif
  fs::remove_all(base, ec);
  return rc;
}

// The chain saved for an image and the chain in place can both hold work, so
// opening an image that has a sidecar while another image is open stops and
// asks instead of choosing. The answer decides the chain the image opens with,
// and nothing about the open may change before it arrives.
// Opening decodes on a worker thread now, so a case that opens waits for the
// decode and applies it the way the frame loop does before asserting on it.
static bool finishOpen(Workspace &app) {
  // An open that stopped to ask which chain to use starts no decode, so an idle
  // worker is a pass, not a failure.
  for (int i = 0; i < 4000 && app.opener.busy(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  pumpOpen(app);
  return !app.opener.busy();
}

int testOpenWithSidecarPrompt() {
  auto it = std::find_if(plugins().begin(), plugins().end(),
                         [](const PluginEntry &pe) { return pe.label == "Transform"; });
  if (it == plugins().end()) return fail("open prompt: bundled Transform plugin not found");

  const fs::path base = fs::temp_directory_path() / "ofxrawhost-selftest-open";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);

  // Point the configuration directory at the temp folder, so the settings file
  // an open writes lands there instead of the real user directory.
#ifdef _WIN32
  const char *envKey = "APPDATA";
#else
  const char *envKey = "HOME";
#endif
  const char *oldEnv = std::getenv(envKey);
  std::string oldValue;
  if (oldEnv) oldValue = oldEnv;
#ifdef _WIN32
  _putenv_s(envKey, base.string().c_str());
#else
  setenv(envKey, base.string().c_str(), 1);
#endif

  int rc = 0;
  Image flat;
  flat.w = 8;
  flat.h = 8;
  flat.px.assign((size_t)flat.w * flat.h * 4, 0.5f);
  const std::string imageA = (base / "a.png").string();
  const std::string imageB = (base / "b.png").string();
  const std::string imageC = (base / "c.png").string();
  const std::string images[] = {imageA, imageB, imageC};
  for (const std::string &path : images)
    if (!writeImage(flat, path)) rc = fail("open prompt: the test image was not written");

  // One Transform node per chain, and the zoom tells the chains apart.
  auto makeChain = [&](double zoom) {
    PersistChain chain;
    chain.selectedNode = 0;
    PersistNode node;
    node.pluginIdentifier = it->plugin->pluginIdentifier;
    node.pluginLabel = it->label;
    node.params["zoom"] = JsonValue::makeDouble(zoom);
    chain.nodes.push_back(std::move(node));
    return chain;
  };
  auto openZoom = [](const Workspace &app) {
    const PersistChain chain = captureChain(app.chain);
    if (chain.nodes.empty()) return 0.0;
    auto value = chain.nodes[0].params.find("zoom");
    if (value == chain.nodes[0].params.end()) return 0.0;
    return value->second.d;
  };

  // Image A opens with no other image open, so its chain is restored without an
  // answer. Its sidecar then records the chain that was built for it.
  App app;
  openPath(app, imageA);
  if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
  if (rc == 0 && app.document.path != imageA) rc = fail("open prompt: the first image did not open");
  if (rc == 0 && app.gui.showSidecarPrompt) rc = fail("open prompt: the first image asked");
  if (rc == 0) applyChain(app, makeChain(100.0));
  if (rc == 0 && openZoom(app) != 100.0) rc = fail("open prompt: the chain of the first image did not apply");
  if (rc == 0) saveCurrentInputSidecar(app.document, app.chain);
  if (rc == 0 && !saveInputSidecar(imageB, ColorSpace::sRGB, ColorSpace::sRGB, makeChain(300.0)))
    rc = fail("open prompt: the sidecar of the second image was not written");

  // Image B carries a saved chain of its own, so the open waits for the answer
  // and leaves the document and the chain alone until it arrives.
  if (rc == 0) {
    openPath(app, imageB);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (!app.gui.showSidecarPrompt) rc = fail("open prompt: the open did not ask");
    if (app.gui.pendingOpenPath != imageB) rc = fail("open prompt: the pending path is not the opened image");
    if (app.document.path != imageA) rc = fail("open prompt: the document changed before the answer");
    if (openZoom(app) != 100.0) rc = fail("open prompt: the chain changed before the answer");
  }
  // Yes: the chain recorded in the sidecar wins.
  if (rc == 0) {
    resolvePendingOpen(app, true);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (app.document.path != imageB) rc = fail("open prompt: Yes did not open the image");
    if (openZoom(app) != 300.0) rc = fail("open prompt: Yes did not restore the saved chain");
  }
  // No: the chain in place carries into the image, and the saved one is dropped.
  if (rc == 0) {
    openPath(app, imageA);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (!app.gui.showSidecarPrompt) rc = fail("open prompt: the second open did not ask");
    resolvePendingOpen(app, false);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (app.document.path != imageA) rc = fail("open prompt: No did not open the image");
    if (openZoom(app) != 300.0) rc = fail("open prompt: No did not keep the chain in place");
  }
  // An image with no sidecar needs no answer, and the chain carries into it.
  if (rc == 0) {
    openPath(app, imageC);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (app.gui.showSidecarPrompt) rc = fail("open prompt: an image without a sidecar asked");
    if (app.document.path != imageC) rc = fail("open prompt: the third image did not open");
    if (openZoom(app) != 300.0) rc = fail("open prompt: the chain did not carry into an image without a sidecar");
  }
  // A chain with no plugin holds no work, so the saved chain is restored without
  // a question.
  if (rc == 0 && !saveInputSidecar(imageA, ColorSpace::sRGB, ColorSpace::sRGB, makeChain(500.0)))
    rc = fail("open prompt: the sidecar of the first image was not rewritten");
  if (rc == 0) {
    clearNodes(app);
    openPath(app, imageA);
    if (!finishOpen(app)) return fail("open prompt: the decode did not finish");
    if (app.gui.showSidecarPrompt) rc = fail("open prompt: an empty chain asked");
    if (openZoom(app) != 500.0) rc = fail("open prompt: the saved chain was not restored over an empty one");
  }

  if (rc == 0) std::printf("ok  open with sidecar prompt\n");

#ifdef _WIN32
  if (oldValue.empty()) _putenv_s(envKey, "");
  else _putenv_s(envKey, oldValue.c_str());
#else
  if (oldValue.empty()) unsetenv(envKey);
  else setenv(envKey, oldValue.c_str(), 1);
#endif
  fs::remove_all(base, ec);
  return rc;
}

// The per-user settings file must round-trip the preference fields, so a
// restart restores the theme, font, panel visibility, and export defaults. The
// case points the config directory at a temp folder, so it never reads or
// writes the real user settings.
int testGlobalSettingsRoundTrip() {
  const fs::path base = fs::temp_directory_path() / "ofxrawhost-selftest-config";
  std::error_code ec;
  fs::remove_all(base, ec);

#ifdef _WIN32
  const char *envKey = "APPDATA";
#else
  const char *envKey = "HOME";
#endif
  const char *oldEnv = std::getenv(envKey);
  std::string oldValue;
  if (oldEnv) oldValue = oldEnv;
#ifdef _WIN32
  _putenv_s(envKey, base.string().c_str());
#else
  setenv(envKey, base.string().c_str(), 1);
#endif

  PersistGui saved;
  saved.showPlugins = false;
  saved.showParameters = true;
  saved.showFilmstrip = true;
  saved.previewRes = 3;
  saved.outputIndex = 2;
  saved.exportIndex = 3;
  saved.exportIntent = 2;
  saved.exportQuality = 71;
  saved.exportSidecar = true;

  // The theme and the interface font size belong to the user, so they travel in
  // their own part of the settings file.
  PersistAppearance savedAppearance;
  savedAppearance.themeIndex = 5;
  savedAppearance.uiFontPt = 15;

  PersistUpdates savedUpdates;
  savedUpdates.channel = static_cast<int>(UpdateChannel::Prereleases);
  savedUpdates.checkAtStartup = false;
  savedUpdates.devWarningShown = true;

  int rc = 0;
  if (!saveGlobalSettings(saved, savedAppearance, savedUpdates)) rc = fail("settings: save");
  PersistGui loaded;
  PersistAppearance loadedAppearance;
  PersistUpdates loadedUpdates;
  if (rc == 0 && !loadGlobalSettings(loaded, loadedAppearance, loadedUpdates)) rc = fail("settings: load");
  if (rc == 0) {
    bool same = loaded.showPlugins == saved.showPlugins &&
                loaded.showParameters == saved.showParameters && loaded.showFilmstrip == saved.showFilmstrip &&
                loaded.previewRes == saved.previewRes && loaded.outputIndex == saved.outputIndex &&
                loaded.exportIndex == saved.exportIndex && loaded.exportIntent == saved.exportIntent &&
                loaded.exportQuality == saved.exportQuality && loaded.exportSidecar == saved.exportSidecar;
    if (!same) rc = fail("settings: values did not round-trip");
  }
  if (rc == 0) {
    const bool same = loadedAppearance.themeIndex == savedAppearance.themeIndex &&
                      loadedAppearance.uiFontPt == savedAppearance.uiFontPt;
    if (!same) rc = fail("settings: the theme and the font size did not round-trip");
  }
  if (rc == 0) {
    const bool same = loadedUpdates.channel == savedUpdates.channel &&
                      loadedUpdates.checkAtStartup == savedUpdates.checkAtStartup &&
                      loadedUpdates.devWarningShown == savedUpdates.devWarningShown;
    if (!same) rc = fail("settings: update preferences did not round-trip");
  }

#ifdef _WIN32
  if (oldValue.empty()) _putenv_s(envKey, "");
  else _putenv_s(envKey, oldValue.c_str());
#else
  if (oldValue.empty()) unsetenv(envKey);
  else setenv(envKey, oldValue.c_str(), 1);
#endif
  fs::remove_all(base, ec);
  if (rc == 0) printf("ok  global settings round-trip\n");
  return rc;
}

// The update check compares release tags. A tag carries a "v" prefix and a
// development build carries a "-dev-<sha>" suffix, and neither may be read as a
// different version; two versions of different length must still order.
int testUpdateVersionOrdering() {
  struct Case {
    const char *left;
    const char *right;
    int expected;  // sign only
  };
  static const Case cases[] = {
      {"v0.4.2", "0.4.2", 0},
      {"0.4.2-dev-ac9deb", "0.4.2", 0},
      {"0.4.2-dev-ac9deb", "0.4.2-dev-ffffff", 0},
      {"0.4.2", "0.4.10", -1},
      {"0.5", "0.4.9", 1},
      {"0.4.2-dev-ac9deb", "0.4.1", 1},
      {"v0.4.3-dev-abc", "0.4.2-dev-def", 1},
      {"1.0.0", "0.99.99", 1},
      {"0.4", "0.4.0", 0},
      {"junk", "0.1", -1},
      {"0.1", "junk", 1},
      {"junk", "other", 0},
  };
  for (const Case &c : cases) {
    const int order = compareVersions(c.left, c.right);
    int sign = 0;
    if (order < 0) sign = -1;
    if (order > 0) sign = 1;
    if (sign != c.expected) return fail((std::string("version order: ") + c.left + " vs " + c.right).c_str());
  }

  std::vector<int> parts;
  if (!versionParts("v1.2.3", parts) || parts.size() != 3 || parts[0] != 1 || parts[1] != 2 || parts[2] != 3)
    return fail("version parts");
  if (versionParts("-dev-abc", parts) || !parts.empty()) return fail("version parts accepted a tag with no number");

  // A release object names one version.
  ReleaseEntry one;
  static const char *kRelease =
      "{\"tag_name\":\"v0.5.0\",\"html_url\":\"https://github.com/aaronmurniadi/ofxrawhost/releases/tag/v0.5.0\"}";
  if (!parseReleaseResponse(kRelease, one) || one.tag != "v0.5.0") return fail("release tag");
  if (one.url.find("/releases/tag/v0.5.0") == std::string::npos) return fail("release page url");
  if (parseReleaseResponse("{}", one)) return fail("release: empty object accepted");
  if (parseReleaseResponse("[]", one)) return fail("release: array accepted");
  if (parseReleaseResponse("{\"tag_name\":\"\"}", one)) return fail("release: empty tag accepted");
  if (parseReleaseResponse("not json", one)) return fail("release: junk accepted");
  if (parseReleaseResponse("{\"message\":\"API rate limit exceeded\"}", one))
    return fail("release: rate limit answer accepted");

  // The release list holds both kinds, newest first. A newer version wins, and a
  // pre-release wins a tie because the caller asked for pre-releases.
  ReleaseEntry newest, current;
  static const char *kList =
      "[{\"tag_name\":\"v0.5.0\",\"prerelease\":false,\"html_url\":\"u5\"},"
      "{\"tag_name\":\"v0.6.0-dev-abc1234\",\"prerelease\":true,\"html_url\":\"u6\"},"
      "{\"tag_name\":\"v0.4.9\",\"prerelease\":false,\"html_url\":\"u4\"}]";
  if (!parseReleasesResponse(kList, "0.4.9", newest, current) || newest.tag != "v0.6.0-dev-abc1234" ||
      newest.url != "u6")
    return fail("release list: newest version not chosen");
  if (current.tag != "v0.4.9") return fail("release list: the running build was not found");

  static const char *kSameVersion =
      "[{\"tag_name\":\"v0.5.0\",\"prerelease\":false,\"html_url\":\"stable\"},"
      "{\"tag_name\":\"v0.5.0-dev-ffff\",\"prerelease\":true,\"html_url\":\"pre\"}]";
  if (!parseReleasesResponse(kSameVersion, "0.5.0", newest, current) || newest.tag != "v0.5.0-dev-ffff")
    return fail("release list: pre-release did not win a tie");

  static const char *kNewestFirst =
      "[{\"tag_name\":\"v0.5.0-dev-bbbb\",\"prerelease\":true,\"html_url\":\"b\"},"
      "{\"tag_name\":\"v0.5.0-dev-aaaa\",\"prerelease\":true,\"html_url\":\"a\"}]";
  if (!parseReleasesResponse(kNewestFirst, "0.5.0-dev-aaaa", newest, current) ||
      newest.tag != "v0.5.0-dev-bbbb")
    return fail("release list: same-version development builds are not ordered by list order");

  if (parseReleasesResponse("[]", "0.5.0", newest, current)) return fail("release list: empty list accepted");
  if (parseReleasesResponse("{\"tag_name\":\"v1.0.0\"}", "0.5.0", newest, current))
    return fail("release list: object accepted");
  if (parseReleasesResponse("[{\"name\":\"no tag\"}]", "0.5.0", newest, current))
    return fail("release list: entry with no tag accepted");

  // Two development builds of one version carry the same numbers, so the time
  // their releases were created decides which one is newer.
  static const char *kTwoBuilds =
      "[{\"tag_name\":\"v0.4.2-dev-ec3ace\",\"prerelease\":true,\"created_at\":\"2026-10-11T05:53:41Z\"},"
      "{\"tag_name\":\"v0.4.2-dev-b56123\",\"prerelease\":true,\"created_at\":\"2026-10-10T19:49:27Z\"}]";
  if (!parseReleasesResponse(kTwoBuilds, "0.4.2-dev-b56123", newest, current))
    return fail("release list: the development builds were not read");
  if (!releaseIsNewer(newest, current, "0.4.2-dev-b56123"))
    return fail("release time: the newer development build was not newer");
  if (releaseIsNewer(newest, newest, "0.4.2-dev-ec3ace"))
    return fail("release time: the newest development build was newer than itself");
  ReleaseEntry noTime;
  if (releaseIsNewer(newest, noTime, "0.4.2-dev-b56123"))
    return fail("release time: a build missing from the list was treated as older");
  if (!releaseIsNewer(ReleaseEntry{"v0.4.3-dev-abc", "u", ""}, current, "0.4.2-dev-b56123"))
    return fail("release time: a higher version was not newer");
  if (releaseIsNewer(ReleaseEntry{"v0.4.1", "u", ""}, current, "0.4.2-dev-b56123"))
    return fail("release time: a lower version was newer");
  // A time in another shape must not order anything.
  if (releaseIsNewer(ReleaseEntry{"v0.4.2-dev-ec3ace", "u", "yesterday"}, current, "0.4.2-dev-b56123"))
    return fail("release time: a time that is not a timestamp was accepted");

  // A release build follows releases only, and a development build may follow
  // pre-releases, which is also the channel it starts on.
  if (!channelAllowed(UpdateChannel::Stable)) return fail("stable channel rejected");
  const bool dev = isDevelopmentBuild();
  if (channelAllowed(UpdateChannel::Prereleases) != dev) return fail("pre-release channel allowed on a release build");
  if ((defaultUpdateChannel() == UpdateChannel::Prereleases) != dev) return fail("default channel");

  // The stored channel is clamped when it is applied, so a release build that
  // once ran a development build cannot keep checking pre-releases.
  App app;
  PersistUpdates stored;
  stored.channel = static_cast<int>(UpdateChannel::Prereleases);
  stored.checkAtStartup = false;
  applyUpdates(app.gui, stored);
  if (!dev && app.gui.updateChannel != UpdateChannel::Stable) return fail("stored pre-release channel was not clamped");
  if (dev && app.gui.updateChannel != UpdateChannel::Prereleases) return fail("development build channel was clamped");
  if (app.gui.checkUpdatesAtStartup) return fail("stored start-up preference was not applied");

  printf("ok  update version ordering\n");
  return 0;
}
