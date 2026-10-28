# Restructure plan: hub-users-inspired backend for OfxRawHost

Agent instructions for refactoring OfxRawHost so the backend follows the same
layered pattern as `hub-users` (Ninja Van Go service): interface-per-layer,
constructor injection, a hand-written Bootstrap composition root, and
command-style managers.

This document is the source of truth for the restructure. Follow it in order.
Do not invent a parallel architecture.

**This file is the only restructuring plan in the repo.** Do not look for
`PLAN.md`, design drafts, overnight reports, or `docs/designs/*` — those are gone
on purpose so agents have one source of truth.

Supporting (not competing) references:

- `cmake/check_layers.cmake` — enforced include direction (keep)
- `docs/workspace-format.md` — on-disk workspace / sidecar format (keep)
- `AGENTS.md` — release / push rules (keep)
- Reference pattern outside this repo: `hub-users` (`controllers/` →
  `internal/managers/` → `internal/daos/` → `models/`, wired in
  `controllers/module.go`)

---

## Agent rules (mandatory)

1. **One step per change set.** Finish a step, keep the build green, run
   `OfxRawHostTests` (or `./build/OfxRawHost --selftest` if that is still the
   gate), then stop or continue to the next step. Do not merge steps.
2. **No behavior change** unless a step says otherwise. Pixels, sidecar format,
   workspace format, menus, and shortcuts stay the same.
3. **Keep the existing layer graph.** Do not invent new top-level layers.
   Allowed layers and include direction stay as in `cmake/check_layers.cmake`:

   ```
   core → platform, gfx
        → model, codecs, ofx
        → persist (may use codecs)
        → engine
        → services
        → ui
        → app
   ```

4. **Nothing but `app/` knows `struct App`.** Commands and managers take narrow
   deps or session state, never the window or the preview texture.
5. **Never `git push`** unless the user asks in that request. Ask before any
   release tag or stable release (see `AGENTS.md`).
6. **Never use a ternary** when an `if`/`else` is enough (project agent rule).
7. **Use CodeGraph first** for symbol lookup in this repo before falling back to
   ripgrep.
8. After each step, run:

   ```sh
   cmake -P cmake/check_layers.cmake
   cmake --build build -j8
   ./build/OfxRawHostTests   # or the current self-test entry the CI uses
   ```

9. Prefer small commits that look human-authored. No AI/agent trailers.

---

## Goal

Keep OfxRawHost’s C++ layer directories. Reshape the **backend** so that:

| hub-users | OfxRawHost |
|---|---|
| `controllers/` | `ui/` (thin: dialogs, panels, menu handlers) |
| `route.go` | UI action binding (menu / key → one manager call) |
| `controllers/module.go` Bootstrap | `app/Bootstrap` built from `RunApp` |
| `internal/managers/` | `services/` managers (interfaces + Impl) |
| `internal/daos/` | `persist/daos/` interfaces over sidecar / project I/O |
| `internal/services/` (HTTP) | `platform/` + update adapters |
| `models/` | `model/` plain data structs |
| workers / schedulers | `engine/` + `OpenWorker` |

Target dependency direction:

```
ui → managers (services) → (daos | engine | platform adapters) → model / core
app wires everything; nothing else knows App
```

One-sentence target: **keep the layer graph; make `services/` look like
hub-users’ managers + daos, with `app/Bootstrap` as the composition root and
`ui/` as thin controllers.**

---

## Target tree (backend)

Create new files under these paths as the steps require. Do not delete old free
functions until their call sites move.

```
src/
  app/
    main.cpp
    RunApp.cpp / RunApp.h
    AppState.h                 # App = Workspace + window + texture (until aggregate split)
    Bootstrap.h / Bootstrap.cpp

  ui/                          # controllers — thin
    UiFrame.*
    *Panel.cpp
    actions/                   # optional binders once managers exist
      DocumentActions.cpp
      ChainActions.cpp
      SessionActions.cpp

  services/                    # managers
    managers/
      document/
        DocumentManager.h      # interface
        DocumentManager.cpp    # Impl (may start as facade)
        OpenImageManager.h     # command-style (later split)
        OpenWorkspaceManager.h
      export/
        ExportManager.h
        ExportManager.cpp
      chain/
        ChainManager.h
        ParamEditManager.h
        NodeGraphManager.h
      filmstrip/
        FilmstripManager.h
      session/
        ColorSpaceManager.h
        StateMapping.*         # keep / relocate existing helpers
      updates/
        UpdateManager.h
    dto/                       # request/response value types (optional early)
      OpenImageRequest.h
      ExportRequest.h
      ColorSpaceChange.h
    Workspace.h                # shrink over time; do not grow it

  engine/                      # workers stay
  persist/
    ProjectPersist.*           # keep; become Impl bodies
    daos/
      SidecarDao.h / .cpp
      WorkspaceProjectDao.h / .cpp
  codecs/
  ofx/
  model/
  core/
  platform/
  gfx/
```

Update `CMakeLists.txt` and `cmake/check_layers.cmake` when new files appear so
the `services` and `persist` targets still compile and the layer gate still
passes. New `persist/daos/` files stay in the `persist` layer. New
`services/managers/` files stay in the `services` layer.

---

## Patterns to implement (copy from hub-users)

### 1. Interface + Impl + constructor injection

Every manager and DAO declares an abstract interface next to its Impl. The UI
and other managers depend on the interface only.

```cpp
// services/managers/document/DocumentManager.h
#pragma once

#include <string>

struct DocumentState;
struct GuiState;
struct Filmstrip;

class DocumentManager {
 public:
  virtual ~DocumentManager() = default;
  virtual void openPath(const std::string &path) = 0;
  virtual bool openWorkspace(const std::string &dir) = 0;
  virtual void resolvePendingOpen(bool useSavedChain) = 0;
  virtual bool pumpOpen() = 0;
};
```

The Impl receives DAOs, workers, and status sinks in its constructor. It does
not take `Workspace &` once Step 5 is done. Until then, an adapter Impl may hold
a `Workspace *` set at bootstrap — temporary only.

### 2. Bootstrap composition root

```cpp
// app/Bootstrap.h
#pragma once

#include <memory>

class SidecarDao;
class WorkspaceProjectDao;
class DocumentManager;
class ExportManager;
class ChainManager;
class FilmstripManager;
class ColorSpaceManager;
class UpdateManager;

struct Bootstrap {
  std::unique_ptr<SidecarDao> sidecarDao;
  std::unique_ptr<WorkspaceProjectDao> projectDao;
  std::unique_ptr<DocumentManager> documents;
  std::unique_ptr<ExportManager> exports;
  std::unique_ptr<ChainManager> chain;
  std::unique_ptr<FilmstripManager> filmstrip;
  std::unique_ptr<ColorSpaceManager> colorSpaces;
  std::unique_ptr<UpdateManager> updates;
};

// Builds DAOs → managers. Session/workers are owned by App/Workspace until the
// aggregate split hands them in as constructor args.
Bootstrap initBootstrap(/* narrow deps as steps add them */);
```

`RunApp` calls `initBootstrap`, keeps `Bootstrap` alive for the process, and
passes manager references into the frame / UI. Panels must not construct
managers.

### 3. Command-style managers

Prefer one use-case type per heavy flow (like hub-users’
`sort_app_user_login_manager`). Map today’s free functions:

| Current | Target |
|---|---|
| `openPath` / `pumpOpen` / `resolvePendingOpen` | `OpenImageManager` or `DocumentManager` |
| `openWorkspace` | `OpenWorkspaceManager` |
| `doExport` / `canExport` / `defaultExportName` | `ExportManager` |
| `setInputSpace` / `setPreviewSpace` / `setExportSpace` / `setExportIntent` / `setPreviewRes` | `ColorSpaceManager` (and preview-res if it fits) |
| chain node add/remove/reorder (`NodeGraph`) | `NodeGraphManager` |
| param edits (`ParamEdit` / `ParamBridge`) | `ParamEditManager` |
| filmstrip refresh / navigate | `FilmstripManager` |
| update check start / pump | `UpdateManager` |

Keep a facade manager only when the UI wants one entry point.

### 4. DAO interfaces for disk

```cpp
class SidecarDao {
 public:
  virtual ~SidecarDao() = default;
  virtual bool load(const std::string &imagePath, PersistSidecar &out) = 0;
  virtual bool save(const std::string &imagePath, const PersistSidecar &in) = 0;
};
```

Wrap existing `loadInputSidecar` / `saveCurrentInputSidecar` /
`loadWorkspaceProject` / `persistWorkspace` in Impl classes. Managers call the
DAO, not the free functions, once Step 3 lands.

### 5. Thin UI controllers

UI code:

- reads widgets / file dialogs
- calls **one** manager method
- shows status / opens prompts from returned state or `StatusSink`

UI code must not:

- load sidecars
- decode images
- mutate the chain graph directly
- start export workers except through `ExportManager`

---

## Migration steps

### Step 0 — Inventory (read-only)

**Do.**

- List every free function in `src/services/*.h` and who calls it from `ui/`.
- List every `Workspace &` / `Workspace *` parameter across `src/`.
  Prefer CodeGraph / ripgrep on `Workspace`.
- Confirm layer gate and tests pass on current `main`.

**Accept:** a short inventory in the PR description or a comment on the first
refactor PR. No code change required in this step if the tree is already known.

---

### Step 1 — Add Bootstrap shell (no behavior change)

**Do.**

- Add `src/app/Bootstrap.h` and `Bootstrap.cpp` with an empty / minimal
  `Bootstrap` and `initBootstrap()` that returns empty unique_ptrs or no-ops.
- Call `initBootstrap()` from `RunApp` and keep the `Bootstrap` instance alive
  for the app lifetime.
- Wire CMake so `ofxrawhost` app target compiles the new files.

**Do not.** Move any business logic yet.

**Accept:** build + layer check + self-test green; Bootstrap exists and is
constructed once.

---

### Step 2 — Manager interfaces that wrap existing free functions

**Do.**

- Add interfaces + Impls under `services/managers/` for at least:
  - `DocumentManager` → delegates to `Actions.h` (`openPath`, `openWorkspace`,
    `resolvePendingOpen`, `pumpOpen`, color-space setters as needed)
  - `ExportManager` → delegates to `Export.h`
- Construct Impls in `initBootstrap`.
- Change **one** UI call site per manager to go through the Bootstrap getter
  (or a reference stored on a UI context), leaving other call sites on free
  functions temporarily.

**Do not.** Delete free functions yet.

**Accept:** at least one open path and one export path run through managers;
tests green.

---

### Step 3 — Extract DAOs

**Do.**

- Add `persist/daos/SidecarDao` and `WorkspaceProjectDao` interfaces.
- Implement them by moving or wrapping the existing persist free functions.
- Inject DAOs into Document / Export / Filmstrip managers via constructors.
- Update CMake for new persist sources.

**Do not.** Change sidecar JSON schema or filenames.

**Accept:** managers no longer include persist headers for free functions; they
include DAO headers. Tests green; open / save sidecar / open workspace still
work.

---

### Step 4 — Move bodies into Impls; free functions become thin wrappers or die

**Do.**

- Move logic from `Actions.cpp`, `Export.cpp`, and related service files into
  `*ManagerImpl` methods.
- Either delete free functions or leave one-line wrappers that call Bootstrap
  managers **only if** tests / plugins still need the old symbols.
  Prefer deletion and fixing call sites.
- Move remaining UI call sites onto managers.

**Accept:** `services/Actions.cpp` / `Export.cpp` are gone or are thin shims;
all product UI uses managers; tests green.

---

### Step 5 — Narrow `Workspace &`

**Problem.** `services/Workspace.h` is a god-object: document, gui, chain,
filmstrip, update check, status sink, and workers. Dozens of functions take
`Workspace &` (often named `app`), so a call that needs one field can touch any
other. `model/GuiState.h` also mixes unrelated UI concerns. Managers must not
keep that shape.

**Do.**

- Stop adding `Workspace &` on new manager APIs.
- Managers hold references (document, chain, gui slices, workers, status) set at
  bootstrap / session start instead of taking `Workspace &` on every call.
- Split `GuiState` along real concerns when a call site needs only one slice
  (preview view, export settings, dialogs, appearance, update prefs). Match the
  splits already reflected in persist (`PersistGui` / appearance / updates)
  where that helps.
- UI panels take the manager interfaces they need, not `Workspace &`, except
  where drawing still needs `App` / preview texture (`drawPreviewPanel`).
- Shrink `Workspace.h` over time; do not grow it. Prefer deleting members that
  moved into manager-owned session state.

**Do not.** Reintroduce a single aggregate parameter "for convenience." Do not
revive deleted design docs; keep decisions in this file.

**Accept:** new manager APIs do not take `Workspace &`. Layer check still
passes. Self-test green.

---

### Step 6 — Split command managers where files grow

**Do.**

- Split `DocumentManager` into `OpenImageManager` + `OpenWorkspaceManager` if the
  Impl exceeds a maintainable size (rough guide: hundreds of lines or mixed
  unrelated flows).
- Same for chain: `NodeGraphManager` vs `ParamEditManager` if not already split.
- Facades may remain for UI convenience.

**Accept:** each manager file has a clear single responsibility; tests green.

---

### Step 7 — Optional DTOs at the UI boundary

**Do.**

- Add `services/dto/` request structs (`ExportRequest`, color-space change, …)
  where UI currently passes raw ints / paths with unclear meaning.
- Validate in the manager, not in the panel, when validation is business logic.

**Accept:** clearer call sites; no format change on disk.

---

## What must stay as-is

- Layer order and `cmake/check_layers.cmake` (extend allow-lists only when a new
  subdir still belongs to an existing layer)
- `model/` as data-focused structs (no starting workers from model headers)
- `engine/` ownership of render / export worker threading rules
- Workspace / sidecar on-disk formats (`docs/workspace-format.md`)
- Release rules in `AGENTS.md`
- No HTTP controller layer — the GUI **is** the controller surface

---

## CMake / layer checklist for agents

When adding files:

1. Put manager sources in `ofxrawhost_services` (or the current services target).
2. Put DAO sources in the persist / io target that already owns `persist/`.
3. Put `Bootstrap` in the app executable target (composition root only).
4. Run `cmake -P cmake/check_layers.cmake` — fix upward includes; do not add
   exceptions unless the design calls for one edge and documents why.
5. Do not let `services/` include `ui/` or `app/`.
6. Do not let `persist/` include `services/` or `engine/`.

---

## Definition of done (whole restructure)

- [ ] `app/Bootstrap` constructs DAOs and managers; `RunApp` owns its lifetime.
- [ ] UI open / export / color-space / chain / filmstrip flows go through manager
      interfaces.
- [ ] Disk I/O for sidecars and workspace projects goes through DAO interfaces.
- [ ] No new `Workspace &` on manager APIs (Step 5).
- [ ] Free-function service API is removed or reduced to documented shims.
- [ ] `cmake -P cmake/check_layers.cmake` passes.
- [ ] Full build and self-test pass on macOS (and CI platforms you touch).
- [ ] `AGENTS.md` and `README.md` point only at this file as the restructure plan.

---

## Out of scope

- New image features, new OFX plugins, packaging changes
- Stable / dev GitHub releases (separate; see `AGENTS.md`)
- Rewriting ImGui layout or themes
- Introducing a DI framework (Wire, etc.) — stay with hand-written Bootstrap
- Porting Go code or linking hub-users

---

## How an agent should start a work session

1. Read this file and the current step’s **Do / Do not / Accept**.
2. Check git status; do not work on a dirty unrelated tree.
3. Use CodeGraph to find the symbols you will touch.
4. Implement only that step.
5. Run layer check, build, and tests.
6. Summarize what moved, what still calls free functions, and which step is next.
7. Do not start the next step unless the user asks or the session goal names
   multiple steps explicitly.
