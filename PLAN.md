# Refactor plan: structure and abstraction

Tracking document for the architecture review. Each step is independently shippable:
the build must stay green and `OfxRawHost --selftest` must keep passing after each one.

## Goal

Turn the directory layout into an **enforced module graph**:

- Nothing but `app/` knows `struct App`.
- No module header includes a layer above it.
- Shared value types live in `core/`, not inside a codec module.
- No mutable process-wide singletons on the render path.
- The test suite links the libraries, not the shipping binary, and gates CI.

## Invariants (must hold after every step)

1. `cmake --build build -j8` succeeds with no new warnings.
2. `./build/OfxRawHost --selftest` exits 0 with the same case list.
3. No behavior change unless a step says otherwise.
4. Every arrow in the module graph points down (verified by CMake target links).

## Target module graph

```
app/        composition root: App, runApp, wiring          -> everything
ui/         ImGui panels, GL, dialogs, themes              -> services, model, core
services/   use-cases: document, chain, session, persist    -> engine, io, model
engine/     ChainRenderer, RenderWorker, ExportWorker       -> model, ofx, core
model/      Document, Chain, GuiState, RenderState, Filmstrip -> core, gfx
ofx/        OpenFX SDK adapter                              -> io, core
io/         imgio codecs + persist storage                  -> core
gfx/        GlTexture (leaf, no ImGui, no GL in headers)    -> nothing
core/       Image, ColorSpace, Json, UpdateChannel, Status   -> nothing
platform/   Paths, Http, Console, Report, fonts             -> core
```

---

## Step 1 - `core/` extraction

**Why.** `Image`, `ColorSpace`, `RenderIntent`, `ImageMeta` and `EncodeOptions` live in
`imgio/ImageIO.h`, so `model`, `persist` and `ofx` all reach into the codec module. That
single header is the source of both directory-level import cycles. `Json` has the mirror
problem: `imgio` and `persist` both need it, which makes them mutually dependent.

**Do.**

- New `core/Image.h` + `core/Image.cpp`: the five value types, `linearWorkingSpace`, and
  the three name lookups (`colorSpaceName`, `colorSpaceFromName`, `renderIntentName`).
- New `core/Json.h` + `core/Json.cpp`: moved verbatim from `persist/`.
- New `core/UpdateChannel.h`: the `UpdateChannel` enum, its labels, and its count, so
  `model` does not have to include the thread-owning `UpdateCheck.h`.
- `imgio/ImageIO.h` includes `core/Image.h` and keeps only codec declarations.
- One table drives the color-space names; `kColorSpaces[]` in `AppState.h` is deleted.

**Accept:** `persist` and `imgio` no longer include each other; the four color-space
strings exist once.

## Step 2 - module directories, CMake targets, test target

**Why.** One `add_executable` with 46 sources means nothing stops a wrong-direction
include, and the self-test has to ship inside the product binary.

**Do.**

- Move files to `model/`, `engine/`, `services/`, `ui/`, `app/`, `gfx/`.
- `gfx/GlTexture.h` (leaf) so `model/Filmstrip.h` stops including `ui/`.
- CMake: `ofxrawhost_core`, `_io`, `_ofx`, `_model`, `_engine`, `_services`, `_ui`,
  then `OfxRawHost` (app) and `OfxRawHostTests` (selftest, no `main.cpp`).
- Move the `ofxsLog.cpp` patch to `cmake/PatchOfxSupport.cmake`.
- Delete the empty `src/video/` tree.
- CI: the self-test job fails the build instead of warning.

**Accept:** a wrong-direction include does not compile; `--selftest` still passes; the
deprecated `--selftest` flag on the app binary keeps working for one release
(alias to the test binary) so `build.sh` and the release smoke tests do not break.

## Step 3 - `RenderContext` and `RenderWorker`; delete the render globals

**Why.** `gLatestGen`, `gValueMutex` and `gOnMessage` are the real interface of the
engine. Abort is a counter bump, the status channel is a global `std::function`, and the
worker reads `App` fields while the UI mutates them.

**Do.**

- `engine/RenderContext.h`: abort token (generation counter + `cancel()`), the parameter
  value mutex, and a status sink.
- `engine/RenderPort.h`: `request()`, `recolor()`, `waitIdle()`, `rebuildPreview()`.
- `engine/RenderWorker.{h,cpp}`: the class that owns the thread, the schedule and the
  display buffers, and implements `RenderPort`.
- `ofx/` takes `RenderContext&` instead of the globals; `gLatestGen`/`gOnMessage`/
  `gValueMutex` are removed. `gPlugins` becomes an `OfxRegistry` object (still one
  instance, but owned, not a global).

**Accept:** no `extern` mutable state on the render path; the concurrency self-test
still passes.

## Step 4 - de-`App` the export path

**Why.** `Export.cpp` already captures an `ExportJob`; it is 80% of the way to being a
component. It is the cheapest place to prove the pattern.

**Do.** `engine/ExportWorker`: `ExportRequest` in, `ExportDone` callback out. No `App&`,
no back-pointer into live state.

**Accept:** export works from the UI and from the concurrency self-test without an `App`.

## Step 5 - de-`App` the services, narrow the UI context

**Why.** `App&` is the public API of six modules; every new field on `App` is public.

**Do.**

- `services/DocumentService`, `ChainService`, `Session` take the narrow state units plus
  `RenderPort&` and a status sink.
- `app/UiContext.h`: the aggregate the panels actually need (5 units + ports), so panels
  stop taking `App&`.
- `App` keeps only: window, texture, the state units, the workers, and the services.

**Accept:** `rg 'App&'` outside `app/` returns nothing.

## Step 6 - parameter values as `JsonValue`

**Why.** `ChainIO.cpp` hand-writes JSON escaping and number scanning while
`persist/Json.h` has a `Raw` kind built for exactly that job.

**Do.** `PersistNode::paramsJson` becomes `std::map<std::string, JsonValue>`; the two
hand-rolled helpers are deleted.

**Accept:** a saved project round-trips through the real parser; the sidecar selftest
covers a string, a bool, a scalar and a 2D parameter.

## Step 7 - move plugin policy out of the widgets

**Why.** Spektrafilm preset import, LUT export and reset-to-defaults are domain rules
living in a 505-line immediate-mode file; `drawParam` is 197 lines.

**Do.** `services/PluginPresets` owns import/export/reset. `ParamWidgets.cpp` keeps
drawing and intent only; `drawParam` splits per parameter kind.

**Accept:** the preset and reset self-tests call the service directly, not the widgets.

## Step 8 - remaining cleanup

- Split `ofx/OfxSuites.cpp` per suite: props, params, clips, images, multithread, message.
- Single color-space table already landed in step 1; remove the last duplicated label lists.
- `PLAN.md` records the final graph; README roadmap gets one line.

---

## Status

| Step | Status |
| ---- | ------ |
| 1. `core/` extraction | done |
| 2. Modules, CMake targets, test target | done |
| 3. `RenderContext` / `RenderWorker` | done |
| 4. de-`App` export | done (folded into step 3) |
| 5. de-`App` services, `UiContext` | done |
| 6. `JsonValue` parameter values | done |
| 7. Plugin policy out of widgets | done |
| 8. Suite split, cleanup | done |

## Progress notes

### Step 2 landed

The module targets now exist and the tree is reorganised. Two deliberate
deviations from the text above:

- `core/FileFormats.h` was split out of `imgio/ImageIO.h` so the project writer
  asks "is this a supported image?" without including the decoder interface.
- The self-test is an object library, not only a binary: `OfxRawHostTests` links
  it, and the Windows build also links the objects into the single-file `.exe`
  so `--selftest` still runs in place there. Every other platform forwards
  `--selftest` to the sibling binary, which the macOS bundle and the AppImage now
  ship.

Remaining upward edges, all of them `App&` and all removed by steps 3 and 5:

```
engine/ParamBridge.h      -> app/AppState.h
engine/RenderScheduler.h  -> app/AppState.h
services/Actions.h        -> app/AppState.h
services/Export.cpp       -> app/AppState.h
services/NodeGraph.h      -> app/AppState.h
services/Session.cpp      -> app/AppState.h, ui/Themes.h
ui/ParamWidgets.h         -> app/AppState.h
ui/UiFrame.h              -> app/AppState.h
ui/ParamEdit.cpp          -> app/AppState.h
ui/ImGuiBackend.cpp       -> app/AppState.h
```

### Steps 3 and 4 landed

- `core/RenderContext` replaces `gLatestGen`, `gValueMutex` and `gOnMessage` with
  named operations (`beginPass`, `cancelInFlight`, `isCurrent`, `values`,
  `postMessage`). No extern mutable state is left on the render path.
- `gPlugins` is now `OfxRegistry`, reached through `ofxRegistry()` / `plugins()`.
- `engine/RenderWorker` owns the thread, the schedule and the display buffers,
  and takes a `RenderHost` instead of an `App&`. The worker's `request`,
  `recolor`, `rebuildPreview` and `pumpUpload` replaced the four free functions.
  `RenderScheduler.{h,cpp}` and `RenderState.h` are gone.
- `engine/ExportWorker` takes an `ExportJob` and reports an `ExportResult`
  through a callback; `App::exportThread` / `exportInFlight` are gone.
- Step 4 was done together with step 3 because both changed the same members.

### Step 5 landed

Deviation from the text above, deliberate and cheaper than the plan:

- The narrow aggregate is `services/Workspace` (not `app/UiContext`). It holds
  the state units, the two workers, the render host, and the status, and it is
  what every command and most panels take. `app/AppState.h` is now
  `struct App : Workspace { GLFWwindow *window; GlTexture tex; }`.
- Because `App` derives from `Workspace`, the call sites did not move: a command
  that declared `Workspace &app` accepts an `App` unchanged. That kept the step
  to a rename plus three signature changes.
- `App&` now appears only in `ui/UiFrame.{h,cpp}` and `ui/PreviewPanel.cpp`, the
  two places that draw the preview and close the window.
- `services/ParamBridge` moved out of `engine/`: it mutates plugin parameters and
  notifies plugins, which is a use-case, not rendering.
- `Session` no longer includes `ui/Themes.h`; the theme count lives in
  `model/GuiState.h` with a `static_assert` in `ui/Themes.cpp`.

Remaining upward edges, both intentional:

```
app/main.cpp      -> selftest/Selftest.h   (Windows in-app self-test only)
ui/UiFrame.h      -> app/AppState.h        (the window and the preview texture)
```

### Step 6 landed

`PersistNode::params` is `std::map<std::string, JsonValue>`. The two hand-rolled
helpers in `services/ChainIO.cpp` (`paramValueJson`, `applyParamValueJson`) now
build and read a `JsonValue` directly, and `testStateFilesAreAppLevel` asserts a
string, a bool, a scalar and a two-element vector survive the sidecar round-trip.

### Step 7 landed

- `services/PluginPresets` owns the spektrafilm file work: `isHostRunButton`,
  `importSpektrafilmPreset(node, path)`, `runSpektrafilmLutExport`,
  `placeLutExport`. The widgets keep the dialogs and the status text.
- `ui/ParamEdit` moved to `services/ParamEdit`: the post-edit invariant (lock,
  notify, re-derive the chain color spaces, schedule) is a use-case, and the
  reset self-test now includes the service directly instead of a widget header.
- `drawParam` split into one row builder per parameter kind
  (`drawPushButtonRow`, `drawChoiceRow`, `drawScalarRow`, `drawBoolRow`,
  `drawStringRow`, `drawVectorRows`) with one dispatch and one post-edit call.

## Final state

All eight steps are in. A fresh out-of-tree Release build succeeds, and
`OfxRawHostTests` passes all 88 cases, including every bundled and installed
plugin, the state round-trips and the concurrency case. `OfxRawHost --selftest`
forwards to it on macOS, Linux and the AppImage, and runs in place in the
single-file Windows build.

### Module graph as built

```
app/        App (window + texture) + main + RunApp          -> everything
ui/         panels, widgets, themes, dialogs                -> services, model, ofx, gfx, core
services/   Workspace, document/chain commands, persistence,
            preset + parameter policy, update check         -> engine, ofx, io, model, core
engine/     ChainRenderer, RenderWorker, ExportWorker,
            RenderHost, RenderSchedule, jobs                -> ofx, io, model, gfx, core
ofx/        suite files (props, params+effects, runtime), plugins,
            Metal bridge                                    -> io, platform, core
io/         imgio codecs + persist storage                   -> core, platform
model/      Document, Chain, GuiState, Filmstrip             -> core, gfx
gfx/        GlTexture                                        -> core, glfw
core/       Image, Json, RenderContext, Version, UpdateChannel, Perf -> nothing
platform/   Paths, Http, Console, Report, fonts              -> core
```

CMake enforces it: `ofxrawhost_core`, `_gfx`, `_platform`, `_io`, `_model`,
`_ofx`, `_engine`, `_services`, `_ui`, `ofxrawhost_selftest`, then
`OfxRawHost` and `OfxRawHostTests`. A new `#include` that points upward fails to
compile.

Two upward edges remain and are documented as intentional: `app/main.cpp ->
selftest` (the Windows in-app self-test) and `ui/UiFrame.h -> app` (the window and
the preview texture).

### Ternary cleanup and the style gate

The 82 conditional operators that were in the tree are gone: every one now reads
as `if`/`else`, with the behavior unchanged (the self-test covers the plugins,
the decoders, the suites and the state files). Three of them were in
`plugins/Transform.cpp`, which the `src/`-only scan missed at first.

The rule is enforced in three places now:

- `.clang-tidy`: a small, currently-clean check set, with
  `readability-avoid-nested-conditional-operator` as an error.
- `scripts/check-ternaries.py`: the plain `?:` operator, which clang-tidy has no
  check for. It blanks comments and literals before scanning, so a `?` in text is
  not a hit.
- `.github/workflows/quality.yml`: runs the script on every push and pull
  request, and `build.sh` runs it before a local build.
- `Workspace` is a single aggregate rather than five narrow parameters per
  command. It is narrower than `App` (no window, no GL, no status-only surface)
  and the call sites stayed put, which is what made the step affordable.

## Note on the starting state

The working tree was already ahead of `HEAD` when this plan started: `ChainIO`
had been moved out of `persist/` to `src/`, and `persist/DocumentActions.{cpp,h}`
had been deleted, both uncommitted. Those two changes are not part of this
refactor but they appear in the same diff. `git diff HEAD` therefore shows more
than the eight steps listed above.

## Review of the changes that were already in the tree

Two uncommitted changes were waiting when this started, both in the persistence
seam. They are the ancestor of steps 1 and 5, not a separate refactor, and they
hold up.

**What they did.** `persist/DocumentActions.{h,cpp}` (the `App`-coupled mapper for
the settings, theme, sidecar and workspace) was deleted and replaced by
`Session.{h,cpp}` taking narrow units: `captureGui(const GuiState &)`,
`applyAppearance(GuiState &, ...)`, `persistWorkspace(const DocumentState &, const
GuiState &)`. `persist/ChainIO.*` was lifted out of `persist/` and narrowed to
`captureChain(const ChainState &)` plus `restoreNodeParams(Node &, const
PersistNode &)`. `applyChain` moved out of the adapter into `NodeGraph`, where the
effect lifecycle already lives.

**Why it is aligned.**

- It applies the same rule as step 5 one layer lower: the mapper that needs `App`
  is a service, and `persist/` keeps only storage. After it, no file under
  `persist/` includes `AppState.h`, `model/` or an OFX header.
- Moving `ChainIO` out of `persist/` is right on the merits: it knows `Effect`,
  `Param` and `kOfxParamType*`, so it cannot be storage.
- Moving `applyChain` to `NodeGraph` inverts a dependency the right way. In `HEAD`
  the adapter included `NodeGraph.h` and `RenderScheduler.h`; now `NodeGraph`
  includes the adapter. `services/ChainIO.cpp` depends on neither.
- It also removed a per-node cost: `HEAD`'s `applyChain` called `addNode`, which
  ran `syncChainColorSpaces` and `scheduleRender` once per restored node. The
  current version appends every node, then syncs and renders once. Same end state,
  fewer renders. The self-test's chain-color-space and legacy-migration cases
  cover the result.

**What it left for this refactor.**

- The lifted file landed at the `src/` root, with no module to belong to. Step 2
  put it in `services/`; step 1 gave it `core/Image.h` so it no longer reaches
  into `imgio/` for `ColorSpace`.
- `Session.cpp` still included `ui/Themes.h` for `themeCount()`, an upward edge
  from services to the interface. Step 5 removed it by moving `kThemeCount` to
  `model/GuiState.h`, with a `static_assert` in `ui/Themes.cpp` so the registry
  and the count cannot drift.
- `docs/releases/v0.3.7.md` still names `DocumentActions`. That is a historical
  release note describing what v0.3.7 shipped, so it stays.

No behavior change or dead symbol was found in the pre-existing diff: every
function it removed exists in `services/Session.h` or `services/ChainIO.h`, with
the same validation and clamping, and nothing references the old names.

## Leftovers found while closing out

Not part of the eight steps, but consequences of them, found by re-reading the
packaging path:

- **Windows lost its bundled plugins.** `OFX_EMBEDDED_PLUGIN` was a define on the
  `OfxRawHost` target, but `loadEmbeddedPlugins()` is compiled into
  `ofxrawhost_ofx`. After the target split the define no longer reached the
  translation unit that needs it, so the single-file Windows build would have
  shipped with no Transform, Auto Exposure or Denoise. It now lives on
  `ofxrawhost_ofx` under `if(WIN32)`, and `OfxRawHostTests` gets the same factories
  so it passes without an installed plugin. Not verifiable on this machine; it is
  the one change here that a Windows build must confirm.
- **`build.sh` did not ship the test binary.** The CMake `app` target copied
  `OfxRawHostTests` into the bundle, but `build.sh` assembles the `.app` by hand.
  `--selftest` would have failed in a `build.sh`-produced bundle. It is copied now,
  and `TEST=1 ./build.sh` also runs the suite.
- **`AppRun`** already forwards `"$@"`, so the AppImage smoke test is unchanged.
- **`docs/workspace-format.md`** was already correct: the directory entry holds
  only `activeImage`, and the chain lives in the per-image sidecar.

## Post-push fix: the split-out suites must include their own headers

The first pre-release build after the refactor failed on Linux and Windows, both
on the same class of mistake: a suite file inherited its includes from the single
large `OfxSuites.cpp` and never needed them itself on macOS.

- Linux: `src/ofx/OfxPropsSuite.cpp` used `std::isfinite` and `std::lround`
  without `<cmath>`, and `INT_MIN`/`INT_MAX` without `<climits>`. libc++ on macOS
  pulls them in transitively; libstdc++ on the runner does not.
- Windows: `src/selftest/TestPlugins.cpp` used `M_PI`, which MSVC's `<cmath>` only
  defines behind `_USE_MATH_DEFINES`. A file-local `constexpr double kPi` replaces
  it, so no build flag has to precede every include.

Verified locally by compiling the 38 non-Apple translation units with GCC 16
against the same flags; the check reproduces the runner error when the includes
are removed and passes when they are present. The UI and Apple-only units are
skipped there, because their headers need clang.
