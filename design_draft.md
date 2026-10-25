# Remove the Workspace Aggregate, Split GuiState, and Cut the Comment Bulk

* **Author:** Aaron Purnomo Murniadi
* **Status:** Draft
* **Date:** 2026-10-26

--------------------------------------------------------------------------------

## Overview

`OfxRawHost` is a still-image OpenFX host written in C++17: about 14.5k lines of
first-party code in `src/` and `plugins/`, built as ten CMake targets, released
on macOS, Windows and Linux. It is an open-source project with an external
audience of contributors, and the code is the first thing that audience meets.

Three things make that meeting harder than it needs to be. The whole application
lives in one struct that every function is handed, so a function that needs one
piece of state can reach any other. A second struct of the same shape sits beside
it and is reached into more often than the first. And the source carries
explanatory prose above nearly every declaration — 10.5% of all lines — which
makes the code read as generated rather than as something a person maintains.

This design removes the aggregate, splits the second struct into the concerns it
already conflates, moves the correctness rules that are currently enforced by
prose into the types that will own them, and cuts the comments to a stated
budget with a mechanical gate. It is one branch with one release. It does not add
a feature, and it does not change a file on disk.

The work is smaller than it sounds. Three of the four components the design asks
for already exist in the tree in almost the right shape, and the refactor is
mostly a matter of giving 74 call sites the two or three things they actually
use instead of the box that contains everything.

## The Problem

**One struct is handed to everything.** `src/services/Workspace.h` defines a
struct holding the document, the interface settings, the plugin chain, the
filmstrip, the update checker, two background workers, a render host adapter, an
atomic colour space, a quit flag, a mutex and a status string — sixteen members,
four different threading rules. Seventy-four functions take `Workspace &`, and
the parameter is named `app` even though `App` is a different and narrower type.
`destroyNode(Workspace &app, int index)` needs the chain. Nothing in the compiler
stops it from touching the update checker, the open image, or starting a thread.
The only way to know what a function touches is to read its body, which is
exactly the cost this refactor exists to remove.

**The bigger struct is next door.** `src/model/GuiState.h` holds 37 fields:
three panel flags, six preview-view fields, eight export fields, four update
preferences, six open-flow and dialog fields, theme and font, and two text
filters. It has 252 uses across `src/` and `plugins/`, and `ui/UiFrame.cpp` alone
holds 81 of them. Removing `Workspace` while leaving this untouched would leave
the complaint true one level deeper. `persist/` already separates three of these
concerns into `PersistGui`, `PersistAppearance` and `PersistUpdates`; the model
layer is behind the storage layer, and the split closes that gap.

**The background renderer cannot be given what it needs.** `src/engine/RenderHost.h`
declares eleven pure virtual methods — the facts a render pass needs and the two
buffers it writes. The only implementation is `WorkspaceRenderHost`, fifteen
one-line methods that answer by reaching into the aggregate. The interface exists
to keep the render thread away from the application, and it works, but only
because the aggregate is there to reach into. It moves the coupling rather than
removing it.

**Five correctness rules are enforced by prose.** The colour space the chain
writes is an atomic written on one thread and read on two others. The quit flag
stops both workers, and the shutdown order in `RunApp.cpp` depends on it. Six
sites call `waitIdle()` before mutating the chain because the worker borrows it.
A message sink installed at `RunApp.cpp:115` captures the application by
reference in a process-global `std::function` and is never cleared. Each rule is
correct today and each is documented in a comment, which means each is one
careless edit away from being wrong, and the self-test covers only one of the
five.

**The layer graph does not do what the documentation says.** `PLAN.md` and
`README.md` both state that a wrong-direction `#include` fails to compile. It
does not. Every target inherits `src/` on its include path, so the graph is
enforced at final link and only when the symbol fails to resolve.
`src/model/Filmstrip.cpp` includes `imgio/ImageIO.h` and `persist/ProjectPersist.h`
and calls into both, from a target that links neither, and it builds because
every consumer of that target happens to link them. This refactor creates four
new components that will want to reach sideways into exactly this blind spot.

**The comment volume is the loudest signal that the code was generated.** 1,527
comment-only lines out of 14,483 explain not just the surprising rules but the
obvious ones: four paragraphs above one pure-virtual class, a paragraph per
setter, a nine-line module justification at the top of a header. The same voice
continues outside the code, in a 330-line `PLAN.md` that narrates a completed
refactor and eight rows of internal changelog inside the user-facing roadmap.

The cost concentration is the reason to do all of this in one branch rather than
in pieces: the four changes touch the same files, and doing them separately
means rewriting those files four times.

## Goals

Each goal is stated so that a reviewer can say whether it is met without reading
the diff.

* **G1 — No function takes the aggregate.** `Workspace` is deleted. Every one of
  the 74 call sites takes only the state it uses. A function that needs four or
  more owners takes one purpose-built struct rather than a parameter list.
* **G2 — No translation unit can reach a layer above it.** A CI check fails the
  build when a source file in layer *X* contains an `#include` naming a layer
  above it. The existing violation in `model/Filmstrip.cpp` is resolved, and
  `PLAN.md` and `README.md` stop claiming a guarantee the build system does not
  provide.
* **G3 — `GuiState` is split into six named owners**, and the 252 uses of its
  fields resolve to the owner that owns them. The settings file is unchanged.
* **G4 — `RenderHost` and `WorkspaceRenderHost` are deleted.** A render request
  carries a `RenderInputs` value that holds every fact the pass needs. The render
  owner owns the preview buffer, and the worker tests its own stop flag instead
  of polling the application.
* **G5 — Each of the five prose-enforced rules has a home in a type**, and three
  self-test cases assert the seams the current suite cannot reach: open-prompt
  ownership, chain-output-space ownership, and shutdown ordering with the
  message sink alive.
* **G6 — Comments fall to between 600 and 700 lines**, the seventeen named
  load-bearing comments survive, and a CI check rejects any comment block longer
  than three consecutive lines outside a whitelist.
* **G7 — Names stop contradicting their types.** No parameter is named `app`
  unless its type is `App`. `setStatus`/`getStatus` become
  `reportStatus`/`statusText`, matching the name already used on `RenderHost`.
  `App::tex` becomes `previewTexture`. The two remaining upward exceptions in
  the graph are eliminated or documented as intentional in one place.
* **G8 — The build is green on all three platforms and the behaviour cases
  pass unchanged.** Decode, row order, colour-space arithmetic, plugin output
  against a reference image, the settings round-trip and the concurrency case are
  the referee, and a failure in any of them is a defect rather than an outdated
  test.

## Non-Goals

* **Any change to the on-disk format.** `settings.json`, `_directory.json` and
  the per-image sidecars keep their current fields. Splitting `GuiState` does not
  touch them, because `persist/` already models the same concerns separately and
  the two mapping functions are the only seam. If a persisted key genuinely needs
  renaming, that is its own branch with its own version bump and migration.
* **A language-version bump.** The project is C++17. Designated initializers
  would make the narrow structs more pleasant to write, and adopting them is a
  separate decision with its own cross-platform risk.
* **Splitting files because they are long.** `src/ui/Themes.cpp` is 786 lines and
  is a data table; its length is not a defect and it is not in scope.
* **Encapsulating interface state behind setters.** Immediate-mode interface code
  writes through pointers into the state it draws. `ImGui::Checkbox` takes an
  address, so UI-facing fields stay public and reachable; methods cover
  operations, not field access.
* **A headless interface smoke test.** It would catch crashes only. Rewriting the
  structure-coupled cases before the code they describe covers the seams that
  matter.
* **New features, performance work, or build-time work.** Readability and
  maintainability are the only objectives.
* **A general style-guide rewrite.** The ternary rule and the comment gate are
  the only rules this design adds.

## Open Questions

* **Q1 — The comment budget's exact figure.** The design commits to a range of
  600–700 comment-only lines, derived from subtracting the seventeen
  load-bearing comments from the current 1,527. The exact number is decided when
  the keep-list is finalised, which cannot happen before the pass is done.
  Blocks: completing commit 1.
* **Q2 — The content of the comment-gate whitelist.** The gate needs the list of
  files allowed a block longer than three lines. It starts as the files holding
  the seventeen items, but some of those items sit in files that also carry noise,
  and splitting a file's comments across two rules is worse than allowing the
  file. Blocks: commit 1.
* **Q3 — Where four inline helpers go when `GuiState.h` is deleted.**
  `previewMaxEdge`, `outputSpace(int)`, the `kThemeCount` constant and the three
  `exportUses*` predicates each act on one concern and so belong to one of the new
  headers. Nothing depends on the choice except which headers a panel includes,
  but it has to be made once rather than per file. Blocks: commit 3.
* **Q4 — Whether the OFX value cache moves into `ChainStore`.** Parameter reads
  go through a process-wide cache guarded by `RenderContext`, and `ChainStore`'s
  mutators will already idle the render pipeline. Moving the cache would put the
  guard and the thing it guards in one type; leaving it separates a rule from its
  owner but keeps `RenderContext` unchanged. Deferring it is the cheaper option
  and leaves one prose rule in place. Blocks: nothing; it is a decision to record,
  not a gate.
* **Q5 — What runs the branch on Windows and Linux before merge.** The
  repository has one workflow, `release.yml`, so there is no pre-merge build. The
  previous refactor shipped three platform-specific breaks and each was found by
  a release build. If a pull-request build does not exist by the time commit 3
  lands, the branch's cross-platform claim is untested. Blocks: the assertion in
  G8 that the build is green on all three platforms.

## Critical User Journeys (CUJs)

This branch adds no journey. The journeys below are the ones that already exist
and the "Then" clause states what must still hold after the rewrite. They are
written as manual checks because the interface layer has no automated coverage.

### CUJ 1: Open a RAW and build a chain

* **Given**: the application starts with no image open, and at least one OFX
  plugin is installed or bundled.
* **When**: the user opens a `.dng` or `.raf` file, adds a plugin from the plugins
  panel, and changes one parameter.
* **Then**: the full image and the preview decode with the same pixel values as
  before the branch; the preview redraws after the parameter change; the status
  sink shows the plugin's message rather than an empty bar; the parameter value
  survives a second redraw.

### CUJ 2: Reopen a folder whose image has a saved chain

* **Given**: an image was edited in a previous session and its sidecar records a
  chain that differs from the one currently open.
* **When**: the user opens that image.
* **Then**: the open stops and asks which chain to use, both answers produce the
  chains they describe, and the pending-open state is cleared afterwards so a
  second image does not inherit it. This is the journey whose state moves out of
  the interface in commit 3, so it is the one most likely to regress silently.

### CUJ 3: Export with colour management and a sidecar

* **Given**: an image with an enabled chain and a chosen export colour space.
* **When**: the user exports as PNG and again as JPEG, with the sidecar option
  on.
* **Then**: both files decode to the values the preview showed in the chosen
  space, the quality and bit-depth controls affect the format that supports them
  and no other, the sidecar records the chain that made the file, and the exported
  file is written with the same name the previous branch produced.

### CUJ 4: Change the preview resolution and keep working

* **Given**: an image open at "Fit to preview".
* **When**: the user switches to 1080p, then to Full res, then changes the
  preview colour space.
* **Then**: each change rebuilds the preview once, the render worker is idle
  before the buffer is replaced, the preview panel keeps its zoom and pan, and
  quitting from that state joins both workers without hanging.

## Technical Plan (Architecture)

### The shape after the change

The application stops being one struct that inherits into the composition root.
`struct App : Workspace` is deleted; `App` holds the owners by value and hands
out references to the ones a given piece of work needs.

```text
BEFORE                                   AFTER

            App                          App  (app/AppState.h)
             │ holds                      ├─ status      StatusSink      (core/)
      ┌──────┴───────┐                    ├─ session     Session         (services/)
      │  Workspace   │  16 members        ├─ chain       ChainStore      (services/)
      └──────┬───────┘                    ├─ render      RenderPipeline  (engine/)
             │                            ├─ filmstrip   Filmstrip       (services/)
   74 functions take it by reference      ├─ updates     UpdateCheck     (services/, kept as-is)
             │                            ├─ window      GLFWwindow*
   ┌─────────┼──────────┐                 └─ previewTex  GlTexture
   ▼         ▼          ▼
 services   ui    WorkspaceRenderHost       StatusSink is declared first, so it
                  (11 virtuals)             outlives every worker that reports
                                            into it.
```

Names: `Session` is currently taken by the persistence mapper in
`src/services/Session.h`, so that file is renamed to `src/services/StateMapping.h`
(`captureGui` and `applyGui` keep their names) and the document owner takes
`Session`. Without that rename the codebase ends up with two types called
`Session` meaning different things, which is the class of confusion this design
exists to remove.

### The four owners that replace `Workspace`

| Owner | Layer | Owns | Absorbs |
| --- | --- | --- | --- |
| `StatusSink` | `core/` | a string and a mutex | `Workspace::status`, `statusMutex`, `setStatus`, `getStatus` |
| `Session` | `services/` | `DocumentState`, the open flow, the pending-open prompt, the workspace directory | `Actions.h`'s open functions, `Workspace::doc`, and `GuiState`'s open-flow fields |
| `ChainStore` | `services/` | `ChainState` and every mutation of it | `NodeGraph.h`'s seven free functions, `ParamBridge`'s chain walk |
| `RenderPipeline` | `engine/` | `RenderWorker`, `RenderSchedule`, the display buffers, the derived chain output space, the stop flag | `Workspace::render`, `chainOutputSpace`, `quit`, and `WorkspaceRenderHost` |
| `Filmstrip` | `services/` | its entries, its thumbnail worker, its stop flag | `Workspace::filmstrip`, and its current undeclared `io` dependency |

`StatusSink` sits in `core/` rather than `services/` for a dependency reason
worth stating: `engine/` and `ofx/` both need to report status, and neither may
link `services/`. A sink with no dependencies belongs at the bottom of the graph.

`Filmstrip` moves from `model/` to `services/`. It calls `loadThumbnailRGBA` from
`io/` and reads the workspace directory, both of which `model/` is not allowed to
touch; it lives in `model/` today only because nothing checks.

`UpdateCheck` is not replaced. It already has the shape this design asks for —
its own worker, its own destructor joining that worker, public read-only fields
for the interface, and no back-pointer — so `App` holds it directly and the
branch leaves the type alone. Its preference fields still move, because they are
settings rather than checker state: `updateChannel`, `checkUpdatesAtStartup` and
the two development-build warning flags go to `UpdatePrefs`, and the checker reads
them at `startCheck`. This is the design's own argument applied to a type that
already satisfies it, and it is the reason the preview buffer and the stop flags
are modelled the same way.

### The honest claim about coupling

Removing the aggregate does not reduce the number of edges between the pieces.
Counted from the tree, `Session` needs four owners, `RenderPipeline` needs three,
`ChainStore` needs two, `Filmstrip` needs two, `StatusSink` needs none. The total
is larger than the single aggregate it replaces.

What changes is the radius of any one call. `destroyNode` today can reach the
update checker; as `ChainStore::destroy` it cannot reach anything but the chain,
the render pipeline it must idle, and the sink. And `App` names what it wires,
instead of a struct that silently contains everything. That is the claim this
design makes, and it is the one a reviewer should hold it to.

If a reader expects the total coupling to fall, `Session` will disappoint them:
it is a coordinator by nature. The mitigation is that `Session` holds references
to owners rather than reaching through a shared box, so its four dependencies are
written in one constructor rather than discoverable only by reading bodies.

### `RenderInputs` replaces `RenderHost`

`RenderHost.h` and `WorkspaceRenderHost` are deleted. A render request carries a
value, captured off the live state by `RenderPipeline` before the worker is
signalled:

| Field | Kind |
| --- | --- |
| `const ChainState &chain` | borrowed, not owned |
| `const Image &full` | borrowed |
| `ColorSpace workingSpace` | copied |
| `ColorSpace previewSpace` | copied |
| `int previewMaxEdge` | copied, resolved from `PreviewRes` and the panel size |
| `Image &previewBuffer` | write target, owned by `RenderPipeline` |

The eleven virtuals did not survive as inputs because they were never all inputs.
`projectLongEdge` is derived from `full` during capture. `previewImage` and
`mutablePreviewImage` were the buffer the pass writes, so ownership of that
buffer moves to `RenderPipeline` and only `rebuildPreview` touches it.
`outputSpace` is `RenderPipeline`'s own derived member and is exposed as a
read-only accessor for `Export`. `quitting()` and `reportStatus()` were never
facts about an image; they become the worker's own stop flag and a `StatusSink`
reference held by the worker.

The consequence is that the render path stops knowing that an application
exists, which is what `RenderHost.h` was trying to achieve and could not while
the aggregate was there to reach into.

### The six owners that replace `GuiState`

| Owner | Layer | Fields taken |
| --- | --- | --- |
| `LayoutState` | `model/` | three panel flags, `settingsSection`, `showSettings`, `showAbout`, `showDonate` |
| `PreviewView` | `model/` | zoom, pan, `previewSpace`, `previewRes`, `previewAreaW`, `previewAreaH`, `previewFitSettleTime`, the two text filters |
| `ExportSettings` | `model/` | format, bit depth, quality, lossless, sidecar, space, intent, `showExportDialog` |
| `UpdatePrefs` | `model/` | channel, startup flag, the two development-build warning flags |
| `Appearance` | `model/` | `themeIndex`, `uiFontPt`, `themeApplyPending`, `fontApplyPending` |
| open-flow state | moves to `Session` | `showSidecarPrompt`, `pendingOpenPath`, `pendingWorkspaceDir`, `showResetParamsConfirm` |

The last row is the one with a correctness payoff rather than a cosmetic one.
Half of the open-directory command's control flow currently lives in interface
state and is drained by `drawUiFrame` at `UiFrame.cpp:487-495`. Moving it to
`Session` puts the command and its own pending state in one place, and the
self-test already asserts on two of those fields, so a wrong move fails.

All six remain plain structs with public fields. The interface writes into them
through pointers because `ImGui::Checkbox` and `InputText` require an address;
putting setters in front of every field would make the panel code longer without
making it clearer. Methods appear on the owners that have operations —
`ExportSettings::setSpace`, `Session::open`, `ChainStore::syncColorSpaces`, and so
on.

### Where each prose-enforced rule lands

| Rule today | New home |
| --- | --- |
| `chainOutputSpace` is atomic, written on the interface thread, read by two workers | `RenderPipeline` member, written only by `RenderPipeline::setOutputSpace`, called from `ChainStore::syncColorSpaces` under the existing `renderContext().values()` guard; read through `ColorSpace outputSpace() const` |
| `quit` stops both workers, and the shutdown order depends on it | each owner has `stop()`; the composition root calls `render.stop()` then `filmstrip.stop()`, replacing the set-then-notify-both-condition-variables sequence at `RunApp.cpp:163-171` |
| six sites call `waitIdle()` before mutating the chain because the worker borrows it | the first line of each `ChainStore` mutator; the six call-site calls are deleted, and the rule cannot be forgotten because there is no other way in |
| the message sink at `RunApp.cpp:115` captures the application by reference and is never cleared | a `StatusSink &` held by the render path; `StatusSink` is declared first in `App`, so declaration order guarantees it outlives every worker |
| `RenderContext` is a singleton because the OFX host handle is process-wide | unchanged; the singleton stays, and the comment explaining why stays with it |

### How call sites change

The rewrite is mechanical. Representative examples:

| Today | After |
| --- | --- |
| `void destroyNode(Workspace &app, int index)` | `void ChainStore::destroy(int index)` |
| `Node *selectedNode(Workspace &app)` | `Node *ChainStore::selected()` |
| `void openPath(Workspace &app, const std::string &path)` | `void Session::open(const std::string &path)` |
| `void setExportSpace(Workspace &app, int index)` | `void ExportSettings::setSpace(int index)` |
| `void syncChainColorSpaces(Workspace &app)` | `void ChainStore::syncColorSpaces()` |
| `void drawPluginsPanel(Workspace &app)` | `void drawPluginsPanel(ChainStore &, const PluginRegistry &, StatusSink &)` |
| `void exportImage(Workspace &app)` | `void runExport(Session &, ChainStore &, RenderPipeline &, const ExportSettings &, StatusSink &)` |
| `void drawUiFrame(App &app)` | unchanged; `App` no longer inherits anything |

Two of the seventy-four are larger than the rest: `drawUiFrame` reads 81 pieces of
interface state and `runExport` needs five owners. Both take a purpose-built
struct at four or more owners, which is the rule that keeps a `struct ChainRef
{ ChainState &chain; }` from appearing.

### The layer gate

A compile-time gate is not reachable without giving up the `layer/file.h` include
style: that style requires `-Isrc` on every target, and `-Isrc` grants every
layer. So the gate is a configure-time check.

`cmake/check_layers.cmake` holds the allowed-edge table, walks `src/` for
`#include "<layer>/` lines, and fails the configure step on a violation outside a
short whitelist. The whitelist starts with the two documented exceptions and is
expected to shrink. `build.sh` and the release workflow both invoke it, matching
the existing pattern of a small mechanical check rather than a human review rule.

The first thing the gate does is fail, on `model/Filmstrip.cpp`. Fixing that by
moving the file to `services/` is the first use of the gate rather than a
separate cleanup.

### Commit staging inside the one branch

The branch is released once. It is reviewed and bisected in four commits, in this
order, each building and green before the next begins.

| Commit | Contents | Revert boundary |
| --- | --- | --- |
| 1 | Comments cut to budget with the keep-list intact; renames (`setStatus`/`getStatus`, `App::tex`, `doc`, `imgio`, the `Session` rename); `PLAN.md` and `README.md` corrections; the comment-length gate | entire commit is behaviour-free and reverts alone |
| 2 | `StatusSink`; `chainOutputSpace` and the stop flags move; `RenderInputs` replaces `RenderHost`; the shutdown sequence simplifies; three new self-test cases | reverts alone; this is the commit that must be green before commit 3 starts |
| 3 | `Session`, `ChainStore`, `RenderPipeline`, `Filmstrip` in their final homes; the six `GuiState` owners; all 74 call sites; `Filmstrip.cpp` moves to `services/`; the layer gate goes live | the only commit that cannot be partially reverted |
| 4 | The remaining documentation: `PLAN.md`'s step section rewritten to describe the state that now exists, roadmap rows collapsed | reverts alone |

Commit 2 is deliberately heavier than a pure cleanup. Moving the threading rules
before the split means the split happens against a tree whose invariants already
have owners, so a mistake in commit 3 shows up as a compile error or a failing
case rather than as a race that appears at shutdown.

## Alternatives Considered

### Alternative A: Keep the aggregate, fix only the names and the comments

Cut the comments, rename `Workspace &app`, and stop there. This is the cheapest
version of the work and it addresses two of the three complaints directly.

* **Pros**: roughly two days. No threading change, no call-site churn, nothing
  that the self-test cannot see. Fully revertable commit by commit.
* **Cons**: the reach problem is untouched. `destroyNode(Workspace &)` still
  holds the update checker, the open image and both workers, and the next
  function added to the file still cannot be read without its body. The design's
  own goal G1 stays unmet.
* **Reason for rejection**: the owner named the god object as a second, separate
  complaint, not as a symptom of the comments. Fixing the smaller half of a
  stated problem is not a cheaper solution, it is a different and incomplete one.

### Alternative B: Land it incrementally, one layer per release

What `PLAN.md` did for the previous refactor: eight steps, each merged and
released on its own.

* **Pros**: every step is reviewable in isolation and revertable on its own. The
  previous refactor is the evidence that the pattern works. A contributor can
  follow the reasoning one commit at a time. A break on another platform is
  isolated to the step that caused it.
* **Cons**: the same files are rewritten once per step. Commit 2's threading work
  touches `RunApp.cpp`, which commit 1 also touches for renames, which commit 3
  touches again for the split. Each release carries half-finished naming, so the
  project ships a state where `Workspace` is half-emptied for a month.
* **Reason for rejection**: the owner chose one branch. The cost the incremental
  path avoids is real, and the mitigation kept from it is the four-commit split
  inside the branch, which preserves bisectability without releasing a
  half-converted tree.

### Alternative C: Keep the aggregate but hand out narrow view structs

Leave `Workspace` where it is and give each use case a view holding references to
the three or four members it needs — `ChainRef { ChainState &chain; RenderWorker
&worker; }`.

* **Pros**: no ownership change, no constructor wiring, no lifetime questions.
  The diff is mechanical and the threading rules keep their current homes.
* **Cons**: the view is the aggregate wearing a smaller name. A function given
  a three-reference view can reach three owners, and the only rule about which
  view to build is convention. It also makes the count of parameters worse, not
  better, since the caller has to assemble the view. This is the pattern most
  likely to grow back into a box within two years.
* **Reason for rejection**: it moves the problem into type names without reducing
  reach, and it leaves every threading invariant in prose.

### Alternative D: Rebuild the render adapter from five references

Keep `RenderHost` and construct `WorkspaceRenderHost`-equivalent from the owners
at the composition root.

* **Pros**: the smallest possible change to the render path. The worker keeps its
  current blocking loop, so the highest-risk code in the branch is untouched.
* **Cons**: an adapter built from five references is the aggregate with a longer
  constructor. It preserves `quitting()` polling, which is the specific thing
  that makes the shutdown sequence at `RunApp.cpp:163-171` fragile.
* **Reason for rejection**: it retains the coupling the design exists to remove,
  and it keeps the shutdown rule in prose.

### Alternative E: Delete `RenderHost` and pass the pieces at every call

No `RenderInputs`. Each render call takes the chain, the image, the spaces and
the buffer directly.

* **Pros**: nothing new is introduced. There is no struct to keep in step with
  the worker's needs.
* **Cons**: six parameters at every call site, and the preview buffer stays owned
  by the application while the worker writes into it, which is the lifetime rule
  the current comments spend four lines explaining. Adding a seventh fact to a
  render pass then changes every call site.
* **Reason for rejection**: it is the same information as `RenderInputs` with the
  name removed, and it loses the one place where the borrow rule can be written
  down.

### Alternative F: Put setters in front of the interface state

Have each owner expose `setQuality(int)`, `setLossless(bool)` and so on, so no
panel writes a field directly.

* **Pros**: a single place to validate or notify on every change, and the fields
  stop being writable from anywhere.
* **Cons**: immediate-mode interface code needs an address. `ImGui::Checkbox`
  takes a `bool*`; the setter form needs a temporary, a call, and a write-back
  per widget, across roughly fourteen address-of sites and two text buffers. The
  panel code gets longer and no clearer.
* **Reason for rejection**: it makes the code it is meant to improve worse, and
  the validation it promises has no requirement behind it.

### Alternative G: Rewrite the tree from scratch with a clean structure

The obvious response to "this reads as generated" is a fresh start.

* **Pros**: no migration, no compatibility with decisions made in the first
  version, no need to preserve the existing shapes at all.
* **Cons**: 14.5k lines, 21 releases and a working OFX host ABI. There is no
  referee for a rewrite — the 88 cases would be rewritten too, so nothing can
  tell a correct rewrite from a broken one. Cross-platform issues found in this
  project so far (a missing `<cmath>` on Linux, `M_PI` on MSVC, a lost plugin
  define on Windows) took a release each to surface; a rewrite multiplies that
  by the size of the tree.
* **Reason for rejection**: the parts that work — the layer graph, the plugin
  suite, the render worker, the filmstrip, the update checker — would be
  discarded along with the parts that do not. Three of the four owners this
  design needs already exist and only need methods instead of free functions.

### Alternative H: Enforce the layer gate with a compiler or a static analysis tool

Use per-target include directories, or an include-what-you-use pass, instead of a
configure-time script.

* **Pros**: a compiler error is harder to ignore than a script failure, and an
  existing tool has no maintenance cost of its own.
* **Cons**: per-target include directories require `#include "Image.h"` instead
  of `#include "core/Image.h"` across the whole tree, which on its own is a
  larger rename than everything else in this design. IWYU is a heavy dependency
  for one rule and reports far more than layer direction.
* **Reason for rejection**: either option costs more than the rule is worth. The
  script is a dozen lines against an explicit table, and the table is the useful
  artifact because it records which upward edges are intentional.

### Alternative I: Rewrite the structure-coupled tests after the code passes

Treat every failing case as an outdated test and update it to match the new
tree.

* **Pros**: no test work during the refactor, and no time spent deciding which
  case is which.
* **Cons**: after one such rewrite, a behaviour regression and an updated
expectation are indistinguishable. The eleven behaviour cases are the only thing
  that can detect a mistake in commit 3, which has no other referee because the
  interface layer has no coverage.
* **Reason for rejection**: it spends the referee. The design keeps the
  behaviour cases unchanged and rewrites the structure-coupled cases before the
  code they describe, so each rewritten case is red against the old tree and
  green against the new one.

--------------------------------------------------------------------------------

## Non-Functional Requirements

* **Performance**: no measurable regression in the preview path. The render pass
  replaces four virtual calls per pass with field reads and loses one `quitting()`
  poll per loop iteration. The preview rebuild path is unchanged in what it
  computes. No target is attached because there is no throughput problem to
  solve; the requirement is "not slower", measured by the preview redrawing in
  the same time on the same image as the pre-branch build.
* **Rollout and rollback**: one branch, four commits, one release. There is no
  feature flag, because a flag around a structural change has no meaningful
  meaning — the old and new shapes cannot both compile in one tree. The rollback
  unit is the commit: commits 1, 2 and 4 revert alone; commit 3 is the only one
  that requires reverting the branch.
* **Compatibility**: no on-disk change and no library ABI, so there is nothing
  for a user to migrate. A file written by the released version loads in this
  build and the reverse.
* **Concurrency**: the five rules listed in the Technical Plan are the whole
  concurrency surface. Three of them become untestable-by-inspection and
  testable-by-case; the shutdown ordering gains a case that does not exist today.

--------------------------------------------------------------------------------

## Additional Resources

* `./concepts_brief.md` — the brainstorm brief this design was drafted from,
  including the six-lens review that produced the blockers.
* `./CMakeLists.txt` — the ten targets and their link edges.
* `./PLAN.md` — the previous refactor's record, including the three
  platform-specific breaks that inform the verification plan.
* `./docs/workspace-format.md` — the persisted shapes this branch does not change.
* `./src/engine/RenderHost.h`, `./src/services/Workspace.h`, `./src/model/GuiState.h`
  — the three files the branch deletes, kept here as the starting point for a
  reviewer who wants to check that nothing in them was dropped.

## Future Improvements

* **The idea**: adopt C++20 for designated initializers. The narrow structs this
  design introduces are written as six-field aggregates, and designated
  initializers would make those literals readable at the call site. Deferred
  because it is a cross-platform risk on its own and belongs in a branch that
  does nothing else.
* **The idea**: a headless interface smoke test. Both checks in the verification
  plan's manual pass are judgement calls. A null-backend ImGui frame that calls
  each panel once would catch a mis-wired pointer without needing a human, at the
  cost of about 150 lines and a second code path that only the test uses.
* **The idea**: split the self-test by module. One file per layer would let a
  contributor run the cases for the module they changed; today `TestState.cpp`
  is 637 lines covering state, fonts, settings and update ordering.
* **The idea**: move the theme table out of `Themes.cpp`. It is 786 lines of
  data in a source file. This is not a defect, but a data file would let a
  contributor add a theme without reading C++.
* **The idea**: one build workflow for pull requests. Q5 in Open Questions is
  really this item: the cross-platform claim in G8 depends on a CI run that the
  repository currently only triggers on release.

--------------------------------------------------------------------------------

## Detailed Implementation Plan

Four commits in one branch. Files are grouped by the commit that touches them, so
the order of this section is the order of the work.

### Commit 1 — comments, renames, comment gate, documentation corrections

#### New files

#### 1. `cmake/check_comments.cmake` (Create)

* **Type / Scope**: CMake script, build tooling.
* **Proposed Changes**: Walk every source file under `src/` and `plugins/`,
  find runs of consecutive comment-only lines, and fail with the file and line
  when a run exceeds three lines and the file is not in a short whitelist. The
  whitelist holds the files that legitimately need a long block: the ones
  carrying the seventeen load-bearing comments, plus `docs/workspace-format.md`
  is out of scope because the script only reads source. Invoked from
  `CMakeLists.txt` at configure time and from `build.sh`, so a local build fails
  the same way the release build does. Why a script and not a review rule: the
  repository already replaced a review rule (the ternary scan) with nothing when
  it became upkeep, and the comments will regrow unless something mechanical
  objects.

#### 2. `src/codecs/` (Create, replacing `src/imgio/`)

* **Type / Scope**: Directory rename, 8 files: `FileFormats.h`, `ImageIO.h`,
  `ImageIOPriv.h`, `ImageColor.cpp`, `ImageLoad.cpp`, `ImageWrite.cpp`,
  `NoiseProfiles.h`, `NoiseProfiles.cpp`.
* **Proposed Changes**: Move the directory and leave the file names alone, so
  `codecs/ImageIO.h` is the only stutter and it is confined to one path. Update
  the 21 files that include something from it: `engine/ChainRenderer.h`,
  `engine/ExportWorker.cpp`, `model/Document.h`, `model/Filmstrip.cpp`,
  `persist/ProjectPersistSave.cpp`, `selftest/` (four files),
  `services/Actions.cpp`, `services/Export.cpp`, `services/ParamBridge.cpp`,
  `ui/PluginsPanel.cpp`, and the eight moved files themselves. Update
  `CMakeLists.txt` (`ofxrawhost_io` sources) and `README.md:321`. Why: `README.md`
  names this directory to contributors as part of the layout, so it is the one
  abbreviation a newcomer is told to learn, and the substitution is a path change
  inside files already being edited. `FilmstripEntry::tex` is deliberately not
  renamed in the same pass: it does not contradict its type, and stretching the
  naming goal to cover every abbreviation would justify a much larger diff.

  The name is `codecs` and not `imageio` because `imageio` does not build on
  macOS. `src/` is on every target's include path and the filesystem ignores
  case, so a directory named `src/imageio` answers to `<ImageIO/ImageIO.h>`,
  which is the macOS framework header that `AppKit` pulls into
  `src/ui/MacPinch.m`. The compiler then reads the project's C++ header instead
  of the system one and fails on `<string>`. This was found by building the
  branch, and it is the third platform-specific break of exactly this kind in
  the project's history.

#### 3. `src/services/StateMapping.h` and `.cpp` (Create, replacing `Session.h` and `Session.cpp`)

* **Type / Scope**: Service module rename.
* **Proposed Changes**: Move the file and keep the four entry points unchanged
  (`captureGui`, `applyGui`, `captureChain`, `persistWorkspace`). Update the five
  includers: `app/RunApp.cpp`, `persist/ProjectPersist.h`,
  `selftest/TestState.cpp`, `services/Actions.cpp`, `ui/UiFrame.cpp`. Why: the
  name `Session` is needed for the document owner in commit 3, and a module whose
  job is mapping state to and from persisted shapes is more accurately called a
  mapping. Done in commit 1 so commit 3 does not have to rename and create in the
  same step.

#### Files modified in commit 1

| File | Change and reason |
| --- | --- |
| `src/services/Workspace.h` | `setStatus` becomes `reportStatus`, `getStatus` becomes `statusText`, matching the name `RenderHost::reportStatus` already uses. The commit-2 and commit-3 removals from this file land later; commit 1 only renames. |
| `src/app/AppState.h` | `App::tex` becomes `previewTexture`. Also the header comment shrinks from four lines to one. |
| `src/app/RunApp.cpp` | Follows the two renames above, plus the `doc` rename. |
| `src/ui/PreviewPanel.cpp` | Reads `previewTexture` instead of `tex`. |
| `src/model/Document.h` | `DocumentState` stays, but its include becomes `core/Image.h` for `ColorSpace` and `ImageMeta` instead of reaching into `codecs/`. This is the model-to-io edge with no reason; it costs one line and removes one undeclared dependency before the layer gate exists. |
| `src/services/Actions.cpp`, `Export.cpp`, `NodeGraph.cpp`, `ParamBridge.cpp`,  `StateMapping.cpp`\* | `doc` becomes `document` at every use. |
| `src/ui/UiFrame.cpp`, `PluginsPanel.cpp`, `FilmstripPanel.cpp`, `PreviewPanel.cpp` | Same rename. `UiFrame.cpp` holds 81 of the `gui.` uses and the most panel-side `document.` reads. |
| `src/selftest/TestState.cpp`, `TestPlugins.cpp`, `TestConcurrency.cpp` | Same rename, so the tests compile against the renamed member. No assertion changes in commit 1. |
| Every file under `src/` and `plugins/` (125 files) | Comment pass to the 600–700 line budget. The volume is concentrated in `engine/RenderHost.h`, `services/Workspace.h`, `services/Actions.h`, `services/NodeGraph.h`, `model/GuiState.h`, `core/Image.h`, `engine/RenderSchedule.h`, `ofx/OfxSuitesPriv.h`. The seventeen items on the keep-list are excluded by name and reviewed one by one rather than by file. |
| `PLAN.md` | The step-5 section describes a `Workspace` shape this branch deletes. Rewritten in commit 4 to describe what exists after the branch, not what was planned before it. In commit 1 only the false claim about compile-time layer enforcement is corrected, because it is the one sentence a contributor acts on. |
| `README.md` | Line 321 (`imgio` in the layout description) and line 384 (the false "wrong-direction include does not compile" claim). Roadmap rows are commit 4. |
| `CMakeLists.txt` | `imgio` path rename, `Session` path rename, and the new `check_comments.cmake` invocation. |
| `build.sh` | Invoke `check_comments.cmake` before building, so a local build fails the way the release build does. |

\* `Session.cpp` is the file that becomes `StateMapping.cpp` above; listed here because its `document` rename happens in the same pass.

### Commit 2 — status sink, threading ownership, `RenderInputs`

#### New files

#### 4. `src/core/StatusSink.h` (Create)

* **Type / Scope**: Core header, no dependencies.
* **Proposed Changes**: A type holding a string and a mutex, with `report(text)`
  and `text()`. No logging, no callbacks, no observer list. It replaces
  `Workspace::status`, `statusMutex`, `setStatus` and `getStatus`. Why `core/`:
  `engine/` and `ofx/` both report status and neither may link `services/`, so a
  sink with no dependencies is the only placement that compiles. Why not a
  callback registry: there is one reader, in the plugins panel, and adding a
  subscription mechanism would be more code than the thing it replaces.

#### 5. `src/engine/RenderInputs.h` (Create)

* **Type / Scope**: Engine header, value type.
* **Proposed Changes**: Six fields, as specified in the Technical Plan: a
  borrowed `const ChainState &`, a borrowed `const Image &` for the full image,
  copied `workingSpace`, copied `previewSpace`, copied `previewMaxEdge`, and a
  borrowed `Image &` write target for the preview buffer. The header documents
  the borrow rule in one line: the chain reference is valid only while the
  render worker is idle, and `RenderPipeline` does not capture an inputs value
  while a pass is running. Why a value rather than an interface: the interface it
  replaces existed to keep the render path away from the application, and it
  failed at that while the aggregate was there to reach into.

#### Files modified in commit 2

| File | Change and reason |
| --- | --- |
| `src/services/Workspace.h` | Loses `status`, `statusMutex`, `setStatus`, `getStatus`, `chainOutputSpace`, `quit`, and `renderHost`. Gains a `StatusSink`. This is the first shrink of the aggregate; the file is deleted in commit 3. |
| `src/engine/RenderHost.h` | Deleted. Every member is either a `RenderInputs` field or a method on the owner that already holds the data. |
| `src/engine/ChainRenderer.h`, `ChainRenderer.cpp` | Take `RenderInputs` instead of `RenderHost &`. The four virtual calls per pass (`fullImage`, `previewImage`, `workingSpace`, `previewSpace`) become field reads. |
| `src/engine/RenderWorker.h`, `RenderWorker.cpp` | Hold their own stop flag and a `StatusSink &`. `request()` takes a `RenderInputs`. The loop tests its own flag rather than calling `quitting()` once per iteration. `rebuildPreview` writes the buffer it was handed instead of asking for `mutablePreviewImage()`. |
| `src/engine/RenderSchedule.h` | The `stopping` member, if it exists, is removed in favour of the worker's own flag. The three mutex-guarded flags and the `pending` atomic stay; the comment explaining why they differ stays with them. |
| `src/engine/ExportWorker.h`, `ExportWorker.cpp` | Take a `StatusSink &` instead of capturing the application. |
| `src/engine/ExportJob.h` | The output colour space is read from the pipeline that owns it, at capture time, instead of being carried on a host interface. |
| `src/services/ParamBridge.cpp` | `syncChainColorSpaces` still walks the chain and still writes under the `values()` guard, but it writes through the pipeline's `setOutputSpace` rather than an atomic on the aggregate. The walk order and the notify-after-unlock comment stay. |
| `src/services/Export.cpp` | Reads `outputSpace()` from the pipeline instead of the aggregate's atomic. |
| `src/core/RenderContext.h`, `RenderContext.cpp` | `setMessageSink` takes a `StatusSink \*` instead of a `std::function`. One less indirect call in the plugin error path and no dangling capture is possible. |
| `src/model/Filmstrip.h`, `Filmstrip.cpp` | Own their stop flag instead of pointing at the aggregate's `quit`. The thumbnail thread reads it directly. |
| `src/app/AppState.h` | `App` loses the two members it inherited that belonged to the aggregate. `StatusSink` is declared first among the members, which is what makes the lifetime rule true by construction instead of by comment. |
| `src/app/RunApp.cpp` | The message sink at line 115 becomes `renderContext().setMessageSink(&app.status)`. Shutdown at lines 163–171 becomes `render.stop()` then `filmstrip.stop()`, then the existing join, wait, save and teardown in the same order. |
| `src/selftest/TestCases.h` | Declares three new cases. |
| `src/selftest/Selftest.cpp` | Registers them in the existing order-dependent sequence. |
| `src/selftest/TestState.cpp` | Two changes: the structure-coupled assertions on `showSidecarPrompt` and `pendingOpenPath` are updated to whatever commit 3 names them, and a new case asserts that a stopped render worker leaves the output space readable. Per the design rule, the first of these is rewritten before commit 3 lands, not after. |
| `src/selftest/TestConcurrency.cpp` | One new case: start a render, stop it, join, and assert the shutdown order — sink alive when the last report arrives, no thread still running, buffers intact. This is the seam the current 88 cases cannot reach. |
| `src/selftest/TestPlugins.cpp` | Reports through a `StatusSink` rather than the aggregate, since the plugin error path is what the sink exists for. |

### Commit 3 — the owners, the `GuiState` split, and the layer gate

#### New files

#### 6. `src/services/Session.h` and `Session.cpp` (Create)

* **Type / Scope**: Service, document owner.
* **Proposed Changes**: Owns `DocumentState`, the open flow, the pending-open
  prompt and its queue, and the workspace directory. Holds references to
  `ChainStore`, `RenderPipeline`, `Filmstrip` and `StatusSink`, taken at
  construction, so its four dependencies are visible in one place. Public
  operations: open a path, open a workspace, resolve a pending open with or
  without the saved chain, set the input colour space, and set the preview
  resolution. Absorbs the four open-flow fields that currently live in interface
  state and the four open functions from `Actions.h`. Why: half of the
  open-directory command's control flow lives in interface state today and is
  drained by a UI function; putting the command and its pending state in one
  place removes a real coupling rather than a cosmetic one.

#### 7. `src/services/ChainStore.h` and `ChainStore.cpp` (Create)

* **Type / Scope**: Service, chain owner.
* **Proposed Changes**: Owns `ChainState`. Absorbs the seven free functions of
  `NodeGraph.h` as methods: `selected`, `clear`, `add`, `destroy`, `move`,
  `setEnabled`, and `applyPersisted`. Each mutator begins by idling the render
  pipeline, which deletes the six call-site `waitIdle()` calls and makes the rule
  impossible to forget. Also owns `syncColorSpaces`, which walks the chain and
  writes the derived output space into the pipeline through its setter. Why: this
  is the owner where the aggregate does the most damage today — eight functions
  that need the chain and nothing else are handed everything.

#### 8. `src/engine/RenderPipeline.h` and `RenderPipeline.cpp` (Create)

* **Type / Scope**: Engine, render owner and lifecycle.
* **Proposed Changes**: Owns `RenderWorker`, `RenderSchedule`, the display
  buffers, the derived chain output space, and the stop flag. Public operations:
  `start`, `stop`, `waitIdle`, `requestRender`, `setOutputSpace`, `outputSpace`,
  and `rebuildPreview`. Captures a `RenderInputs` value before signalling the
  worker. Why: the worker, the schedule and the buffers already behave as one
  unit; naming the unit is what gives the threading rules a home.

#### 9. `src/model/LayoutState.h`, `PreviewView.h`, `ExportSettings.h`, `UpdatePrefs.h`, `Appearance.h` (Create)

* **Type / Scope**: Model headers, five plain structs.
* **Proposed Changes**: One struct per concern, fields moved verbatim from
  `GuiState`, with `ExportSettings` gaining `setSpace` and `setIntent` because
  those two carry validation. `Appearance` also takes `themeApplyPending` and
  `fontApplyPending`, which belong to applying an appearance change rather than
  to a dialog. Why five files rather than one: the point is that a panel includes
  only the struct it draws. Why plain structs with public fields: the interface
  writes through pointers because `ImGui::Checkbox` requires an address.

#### 10. `cmake/check_layers.cmake` (Create)

* **Type / Scope**: CMake script, build tooling.
* **Proposed Changes**: Hold the allowed-edge table for the ten layers, scan
  `src/` for `#include "<layer>/` lines, and fail the configure step on a
  violation outside a whitelist. The whitelist starts with the two documented
  exceptions and is expected to shrink. Invoked from `CMakeLists.txt`, `build.sh`
  and the release workflow. Why now and not in commit 1: its first run must fail
  on `model/Filmstrip.cpp`, and that file only moves in this commit.

#### Files moved

| File | Change and reason |
| --- | --- |
| `src/model/Filmstrip.h`, `Filmstrip.cpp` | Move to `src/services/`. The file calls `loadThumbnailRGBA` and lists the workspace directory, both of which `model/` may not touch. Moving it is what makes the layer gate pass rather than whitelisting the violation. |

#### Files deleted

| File | Reason |
| --- | --- |
| `src/services/Workspace.h` | Replaced by `Session`, `ChainStore`, `RenderPipeline`, `Filmstrip` and `StatusSink`. |
| `src/model/GuiState.h` | Replaced by the five model headers and the open-flow state in `Session`. The three inline helpers it holds move with the state they act on: `previewMaxEdge` to `PreviewView.h`, `outputSpace(int)` and `kThemeCount` to `Appearance.h` or stay with their users. |
| `src/services/NodeGraph.h`, `NodeGraph.cpp` | Absorbed into `ChainStore`. |

#### Files modified in commit 3

| File | Change and reason |
| --- | --- |
| `src/app/AppState.h` | `App` no longer inherits anything. It holds `StatusSink`, `Session`, `ChainStore`, `RenderPipeline`, `Filmstrip`, the window and the preview texture by value, in that declaration order, and constructs the owners with the references between them. |
| `src/app/RunApp.cpp` | Wires the owners in the constructor order, drains the open flow through `Session`, and keeps the teardown order it already has. |
| `src/services/Actions.h`, `Actions.cpp` | Shrinks to the colour-space setters, which are single-field writes and need no owner of their own. The four open functions move to `Session`. |
| `src/services/ParamBridge.h`, `ParamBridge.cpp` | Takes `ChainStore &` and `RenderPipeline &` instead of the aggregate. |
| `src/services/ParamEdit.h`, `ParamEdit.cpp` | Same substitution; the commit-param-edit path needs the chain and the pipeline only. |
| `src/services/Export.h`, `Export.cpp` | `runExport` takes the five owners it actually reads. |
| `src/services/StateMapping.h`, `StateMapping.cpp` | `captureGui` and `applyGui` take six references instead of one `GuiState &`. The persisted structs are untouched. |
| `src/ui/UiFrame.h`, `UiFrame.cpp` | `drawUiFrame` keeps its signature; the body reads through owners. The menu bar needs `closeWindow()` and nothing else from `App`, which removes the `ui → app` exception from the layer table. |
| `src/ui/PreviewPanel.cpp` | Takes `PreviewView &`, `Exporter &` and the preview texture. Holds 21 of the `gui.` reads and 14 address-of sites. |
| `src/ui/PluginsPanel.cpp`, `ParametersPanel.cpp`, `FilmstripPanel.cpp` | Take the owner each panel draws, plus `StatusSink` where they read status. |
| `src/ui/ParamWidgets.h`, `ParamWidgets.cpp` | Take `ChainStore &` and `RenderPipeline &`. |
| `src/ui/ImGuiBackend.cpp`, `src/ui/Themes.cpp` | Read `Appearance` instead of `GuiState`. `Themes.cpp` keeps its 786 lines; only its three `gui.` reads change. |
| `src/selftest/TestState.cpp` | Asserts against the new owners. The two open-prompt assertions were already rewritten in commit 2, in that order deliberately. |
| `src/selftest/TestPlugins.cpp`, `TestConcurrency.cpp`, `Selftest.cpp` | Follow the owner substitution. No assertion changes. |
| `CMakeLists.txt` | New sources in `ofxrawhost_services` and `ofxrawhost_engine`; `model` loses `Filmstrip.cpp`; `cmake/check_layers.cmake` invoked at configure. |
| `build.sh` | Runs `check_layers.cmake` alongside the comment check. |
| `.github/workflows/release.yml` | Both checks run before the build job, so a violation fails the release rather than shipping. |

### Commit 4 — the remaining documentation

| File | Change and reason |
| --- | --- |
| `PLAN.md` | The eight-step plan and its progress notes describe a refactor that has been superseded. Rewritten to describe the structure that now exists, with the module graph corrected to match the built targets. The narration goes; the graph and the invariants stay. |
| `README.md` | The eight internal-refactor rows at lines 378–386 collapse to one row describing this branch. The layout description at 321 uses `codecs`. The layer claim at 384 states what the gate actually enforces. |
| `AGENTS.md` | Gains the two new rules in the workspace: a comment block may not exceed three consecutive lines, and a source file may not include a layer above its own. Both are cheap to state and both are what a future contributor needs to know to pass CI. |
| `docs/workspace-format.md` | Unchanged. Listed here so the reviewer can confirm the branch did not touch it: the format is identical, and the versioning paragraph remains true. |

### Verification Plan

* **Build**: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then
  `cmake --build build -j`. The configure step runs both checks.
* **Self-test**: `build/OfxRawHostTests` — the 88 existing cases plus three new
  ones. Expected total 91. The eleven behaviour cases must be unchanged from the
  pre-branch tree; a failure there is a defect, not an outdated test.
* **Static analysis**: `clang-tidy -p build` over every changed file with the
  existing `.clang-tidy` set, which is kept small deliberately. The ternary rule
  remains a review rule because no check exists for the plain operator.
* **Layer gate**: `cmake -P cmake/check_layers.cmake` returns zero, and the two
  remaining upward edges are either gone or in the whitelist with a reason.
* **Comment gate**: `cmake -P cmake/check_comments.cmake` returns zero, and the
  comment total is between 600 and 700 lines.
* **Manual pass**, once after commit 2 and once after commit 3: launch the app,
  open a RAW, add a plugin, change a parameter, switch the preview colour space,
  change preview resolution, export PNG and JPEG, reopen the folder to confirm
  the sidecar prompt appears and both answers work, then quit and confirm no
  thread outlives the window. This is the only coverage the interface layer has.
* **Cross-platform**: the release workflow builds macOS, Linux and Windows from
  the branch before merge. The previous refactor shipped three breaks that only
  appeared on a platform other than the developer's — a missing `<cmath>` on
  Linux, `M_PI` on MSVC, and a plugin define that stopped reaching its target on
  Windows — so a branch that only builds on macOS is not verified.


