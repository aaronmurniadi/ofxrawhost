# Remove the `Workspace` Aggregate and Cut the Comment Bulk

* **Author:** Aaron Purnomo Murniadi
* **Date:** 2026-10-26
* **Status:** Brainstorm, reviewed by six Goldfish lenses. Not yet a design.

## Overview

`OfxRawHost` is a still-image OpenFX host: roughly 14.5k lines of first-party C++
in `src/` and `plugins/`, ten CMake targets, releases on macOS, Windows and
Linux. The owner's complaint is that the source reads as generator output. His
specific tells are the comment volume and voice, and the flat flow of free
functions that all take the same `Workspace &app`.

The working hypothesis under review:

> Remove the `Workspace` aggregate and give its state explicit owners — a
> session/document component, a render component, a filmstrip component, a
> status sink — each a type with methods, with call sites naming only the state
> they touch, and taking a narrow purpose-built struct when they would otherwise
> take more than a handful of parameters. In the same branch, cut the comment
> bulk back to non-obvious invariants, and rename abbreviations and parameters
> whose name contradicts their type (`Workspace &app`). No on-disk
> compatibility constraint, refereed by the existing self-test plus whatever new
> cases the new seams require.

Constraints the owner set: one branch, flipped at once, not incremental; on-disk
compatibility may break; behavior may change where it improves the result;
binary size and build time do not matter; readability and maintainability do.

## Analysis

### Two premises stated during ideation are false, and several findings depend on them

These were corrected against the tree. They matter because the plan's confidence
rested on them.

**The tree is C++17, not C++20.** `CMakeLists.txt:4` sets
`CMAKE_CXX_STANDARD 17`. There is no `std::stop_token`, no designated
initializers, no `std::span`. Designated initializers are the cheapest way to
write the multi-field struct literals this plan depends on. If the plan wants
them, the language bump is a fifth change riding inside the same branch.

**The layer graph is enforced at link time, not at include time.** Every target
inherits `src/` on its include path through
`target_include_directories(ofxrawhost_core PUBLIC src)`. Nothing stops an
upward `#include`; the compiler only complains if the symbol fails to resolve at
final link. `PLAN.md` ("A wrong-direction `#include` therefore fails to compile")
and `README.md:384` are both inaccurate. There is a live counter-example:
`src/model/Filmstrip.cpp` includes `imgio/ImageIO.h` and `persist/ProjectPersist.h`
and calls `loadThumbnailRGBA` and `listWorkspaceImages`, while
`ofxrawhost_model` links only `core` and `gfx`. It resolves because every
consumer of `model` also links `io` transitively.

Consequence for this plan: the module system will not warn the implementer when
one of the new owners reaches sideways. The plan intends to create exactly the
kind of cross-layer reach the module system cannot detect. A mechanical gate is
needed — a CI grep asserting that no source in layer X contains an `#include`
for a layer above it — plus a decision on `Filmstrip.cpp`, which is either in
`services/` or needs `model` to declare its real edges.

### Blockers

**The branch currently bundles four independent changes with one reverting
unit.** Structure, comment cut, renames, and on-disk freedom are separable. The
owner's "one branch" constrains releases, not commits. The recoverable shape is
four commits in the one branch, each building and green: (1) comments and
renames, no behavior; (2) status sink, chain-output-space ownership, per-worker
stop flags; (3) the component split and its call sites; (4) docs. Commit 1 and 4
revert cleanly on their own; commit 3 is bisectable.

**Removing `Workspace` leaves the actual god object in place: `GuiState`.**
`src/model/GuiState.h` is 37 fields spanning five unrelated concerns — panel
layout, preview view and zoom, seven export options, four update preferences,
nine dialog flags and deferred-command fields, plus theme and font. It is what
most call sites actually reach: 252 uses of `gui.<field>` in `src/` and
`plugins/`, of which `ui/UiFrame.cpp` alone holds 81. If it is not in scope, the
same complaint stays true one indirection deeper, and the branch has spent its
credibility on `Workspace`.

**Resolved: `GuiState` is split in this branch.** The grouping is not an
invention, because `persist/` already models these concerns as three separate
structs — `PersistGui`, `PersistAppearance`, `PersistUpdates` — defined in
`src/persist/ProjectPersist.h` and mapped by `captureGui` and `applyGui` in
`src/services/Session.h`. The model layer is behind the storage layer, and the
split catches it up. Six owners are indicated by the fields themselves:

- layout: three panel flags, `settingsSection`, `showSettings`, `showAbout`,
  `showDonate`;
- preview view: zoom, pan, `previewSpace`, `previewRes`, `previewAreaW`,
  `previewAreaH`, `previewFitSettleTime`, plus the two `char[128]` filters;
- export: format, bit depth, quality, lossless, sidecar, space, intent,
  `showExportDialog`;
- updates: channel, startup flag, and the two development-build warning flags;
- open and dialog flow: `showSidecarPrompt`, `pendingOpenPath`,
  `pendingWorkspaceDir`, `showResetParamsConfirm`, `themeApplyPending`,
  `fontApplyPending`;
- appearance: `themeIndex`, `uiFontPt`, matching `PersistAppearance`.

Splitting `GuiState` does **not** change any file on disk. `PersistGui` is a
separate struct with its own 13 fields; `captureGui` and `applyGui` are the only
seam, so they take six references instead of one and the wire format is
untouched. The earlier worry that splitting `GuiState` forces a format bump is
wrong, and the earlier recommendation of "format unchanged in this branch"
stands.

**`RenderHost` has no stated successor, and it is already the shape the plan
asks for.** `src/engine/RenderHost.h` declares eleven pure virtuals; the
`WorkspaceRenderHost` adapter in `src/services/Workspace.h` implements them
against the aggregate. With no aggregate, an implementer must invent one of
three designs: (a) keep the adapter, constructed from five references at the
composition root — that is `Workspace` with a longer constructor and no gain;
(b) replace it with a per-request `RenderInputs` value struct, threading the
stop flag and the status sink separately — this fits the plan but changes the
worker's blocking loop and therefore the threading; (c) delete the interface and
pass `const DocumentState&`, `const ChainState&` and two `ColorSpace` values per
call. **Resolved: (b).** `RenderHost` and `WorkspaceRenderHost` are both deleted and
replaced by a `RenderInputs` value captured per request. Three consequences have
to be carried into the plan rather than discovered during it:

- The eleven virtuals split into three kinds. Inputs become fields: the borrowed
  `ChainState&`, the full `Image`, three colour spaces, and the preview long-edge
  cap. Two of the eleven are outputs, not inputs — `previewImage` and
  `mutablePreviewImage` are the buffer the worker writes — so the render owner
  takes ownership of the preview buffer and `RenderInputs` only reads it.
  `projectLongEdge` collapses into the capture step, since it is derived from the
  full image the capture already holds.
- `quitting()` polling disappears. The stop flag moves to the render owner and
  the worker tests its own flag, which is the change that makes
  `RunApp.cpp:163-171`'s set-then-notify-both-condition-variables sequence
  replaceable by `render.stop()` and `filmstrip.stop()`.
- `reportStatus` becomes a `StatusSink&` held by the workers rather than a
  virtual on an adapter, and the composition root must declare the sink before
  the workers so it outlives them — the current `[&app]` lambda at
  `RunApp.cpp:115` has no such guarantee.

This is why commit 2 in the staging below now carries the stop-flag and sink
work: choice (b) moves the threading change out of the split and into the commit
that can be tested before the split starts.

**Nothing in this refactor requires a format change, so the permission is the
risk, not the cost.** `settings.json`, `_directory.json` and the sidecars are
produced by `persist/` from `PersistGui` and `PersistChain`. Splitting
`Workspace` does not touch them. If the permission is treated as a licence for
incidental cleanups — `outputIndex` names a preview space, `pluginLabel`, key
spelling — a readability branch becomes a silent user-data migration whose only
referee, `TestState.cpp`, tests the new format. The project shipped v0.4.2
telling users their old sidecars still load. Recommended: the format does not
change in this branch; a key rename needs its own migration paragraph and a
`version: 2` bump, in its own branch.

**The referee is blind exactly where the churn is.** The 88 cases do not call
`drawUiFrame`, any panel, or the export dialog. `ui/` is about 2,300 lines
holding a large share of the state reads being rewritten. A mis-wired panel —
`exportQuality` landing in the bit-depth slot of a struct with two adjacent
`int`s — compiles and passes every case. Two mitigations are affordable and one
is not: keep panel bodies textually mechanical, so the same field names survive
at the same depth and the diff stays a rename rather than a redesign; add three
cases that assert the new service-level seams (open-prompt ownership,
chain-output-space ownership, shutdown ordering) — `TestState.cpp:371-415`
already asserts `showSidecarPrompt` and `pendingOpenPath`, so it fails when that
state moves, which is the intended tripwire; a headless ImGui smoke test is
optional and catches crashes only.

**Tests may be rewritten, but not after the code passes.** The owner's position
is that the suite can be rewritten as the work proceeds, and no case is kept
merely because it exists. That is defensible for the cases that assert
structure — `TestState.cpp`'s assertions on `showSidecarPrompt` and
`pendingOpenPath` are testing a shape this branch is deleting, and keeping them
unchanged would mean keeping the old shape. It is not defensible for the cases
that assert behaviour: decode correctness, row order, colour-space mathematics,
plugin output against a reference image, settings round-trip through the file,
and the concurrency case. Those eleven are the only referee the split has, and a
failure in them after a structural change is a defect, not an outdated test.

The rule that keeps the distinction honest: a structure-coupled case is
rewritten **before** the code it describes, so it is red against the old tree and
green against the new one. A case rewritten after the code already passes proves
nothing, because it cannot distinguish "the test described the old shape" from
"the test caught a real bug".

**The threading invariants have no owner today, so a rewrite can lose them
silently.** Five contracts are held by convention and prose:

- `chainOutputSpace` (`Workspace.h:61`) is written by `syncChainColorSpaces`
  under `renderContext().values()`, read by `RenderWorker.cpp:87,107` and by
  `Export.cpp:43`.
- `quit` (`Workspace.h:62`) stops both workers; shutdown in
  `RunApp.cpp:163-171` sets it, notifies both condition variables, then stops
  render, joins filmstrip, waits the exporter, saves, clears nodes and tears
  down GL.
- Six `waitIdle()`-before-mutate sites in `NodeGraph.cpp` and `Actions.cpp:81`.
- `RunApp.cpp:115` installs `renderContext().setMessageSink([&app](…) { app.setStatus(…); })`,
  a process-global `std::function` capturing a reference to the object being
  dismantled, never cleared.
- The chain is borrowed by the render worker, so mutation requires `waitIdle()`.

The suite covers rendering against graph mutation but not shutdown,
status-from-worker, or sink lifetime. The plan's strongest genuine argument is
that each of these gets a home in a type: the chain output space becomes a
member written only by the chain owner's own sync, read through a
`ColorSpace outputSpace() const`; `waitIdle()` becomes the first line of each
mutator rather than a rule six call sites must remember; `quit` becomes each
worker's own stop flag; the message sink becomes a `StatusSink&` the composition
root declares first so it outlives every worker. These belong in the brief as
acceptance criteria.

### Majors

**Removing the aggregate relocates coupling; it does not reduce it.** Measured
from the tree, the proposed owners need: session/document → document, chain,
filmstrip, render, status (five); render → chain, document, preview space,
preview size, chain output space, status, stop flag (seven); filmstrip →
document path and workspace dir, stop flag, `io`, `gfx`; status → nothing. The
total edge count rises. The composition root still holds all four and wires each
cross-reference, so it is an aggregate with named fields. The defensible claim
is narrower and should be written down at this precision: per-call-site reach
drops, invariants get a home, and the wiring point names what it wires. If any
owner needs two of the others, it should be merged — filmstrip is a view of the
workspace directory and plausibly belongs to the session owner.

**"A struct past a handful of parameters" needs a number.** 74 sites currently
take `Workspace &app`, 14 take `App &app`. About thirty of the functions need
one to three state units; about fourteen need four to six. A `struct ChainRef {
ChainState &chain; }` is worse than `ChainState&`, and applying the rule
uniformly produces roughly 45 one-field structs for a net readability loss.
Setting the threshold at four distinct owners yields ten to fourteen structs and
leaves the narrow functions honestly narrow. `src/engine/ExportJob.h` is the
in-tree precedent for the right size.

**Immediate-mode UI writes through pointers into the state it draws.** There are
fourteen address-of-state sites in `UiFrame.cpp`, two `char[128]` filter buffers
handed to `InputTextWithHint` in `PluginsPanel.cpp` and `ParametersPanel.cpp`,
and `NodeGraph.cpp:19` returns `&chain.nodes[…]`. The plan's "each a type with
methods" fights ImGui: `ImGui::Checkbox("Lossless", &state.export.lossless)`
cannot become `exportSettings.setLossless()` without a temporary and a
write-back, which makes the code worse. The rule to write down: UI-facing state
stays a public field reachable by reference, and methods cover operations —
open, mutate the chain, request a render, report status — not field access.

**The comment cut has no target and the same voice survives in the documents.**
Measured at 10.5% (1,527 comment-only lines in 14,483). Copying the same voice
outside the code: `PLAN.md` is about 330 lines of refactor narration including a
progress section describing a decision this branch reverses, and `README.md:378-386`
carries eight rows of internal refactor changelog in a user-facing roadmap, one
of which this branch contradicts. A budget is needed — about 4 to 5%, or 600 to
700 lines, is honest rather than "invariants only", because magic numbers,
borrow rules and persistence rules legitimately stay. One mechanical gate is
affordable: no comment block longer than three consecutive lines outside a
whitelist, checked in CI with `awk`, matching the pattern `.clang-tidy` already
uses.

**Seventeen comments are load-bearing and must not be cut.** Found by reading,
to be carried as a do-not-touch list: `RenderHost.h:9` (chain borrowed, so
`waitIdle()` first); `RenderSchedule.h:8-13` (`pending` atomic, three flags
mutex-guarded); `RenderWorker.cpp:83-84` (`makePreview` rewrites a preview the
worker may be reading); `RenderWorker.cpp:10-12` (`kDraftMaxEdge`); the
`RenderContext` singleton reason and `values()` guarding Param writes;
`Chain.h:11` and `NodeGraph.cpp:9-10` (`unique_ptr<Effect>` out-of-line special
members); `GuiState.h` rules (`themeIndex` is app-level and never travels in a
project; `uiFontPt` 0 means system; `previewSpace` is a destination, not a
source; `kFitPreviewFallbackMaxEdge`); `Actions.cpp:81` and `NodeGraph.cpp:114`
(why `waitIdle()`); `ParamBridge.cpp:76-98` (walk order, notify after unlock);
`Session.h:5-8` (app-level preferences never travel in a project);
`RunApp.cpp:137-139` (font atlas rebuild frees fonts the running frame points
at); `docs/workspace-format.md` §Versioning.

**Renames are cheap but one is a collision.** `Session` is already taken by
`src/services/Session.h`, the persistence mapper. The new open-flow owner cannot
be called `Session` without renaming that first, or the codebase gains two
`Session` types meaning different things — the failure this branch exists to
fix. Ordering by value: `Workspace &app` disappears with the split anyway;
`setStatus`/`getStatus` should become `reportStatus`/`statusText`, since
`reportStatus` already exists on `RenderHost`; `tex` → `previewTexture` is
eleven lines in three files (the fourteen `e.tex` hits are
`FilmstripEntry::tex`, a different object); `doc` → `document` is about 45 lines
in eleven files; `imgio` → `imageio` costs 26 include lines, 8 file moves, 5
CMake lines, falsifies `README.md:321`, and turns `imgio/ImageIO.h` into the
stuttering `imageio/ImageIO.h`. The only real cost is `git blame` continuity
across 21 releases. These belong in commit 1 and are mechanical.

**Where each new type can live is constrained by edges nobody checked.**
`model/` owns only `Filmstrip.cpp`; `Document.h`, `Chain.h` and `GuiState.h` are
header-only and compile into whichever target includes them. `Document.h:5`
includes `imgio/ImageIO.h` for `ColorSpace`, which now lives in `core/Image.h` —
a model-to-io edge with no reason. A document owner that loads images, writes
sidecars and rescans the workspace directory cannot live in `model/` (no io
edge) or in `persist/` (storage only, by design). It has to be `services/`,
where it joins `Workspace` as a second aggregate-shaped type until `Workspace`
goes. Each new type's target must be decided before coding, in a small table.

### What the tree already has, which the plan should use

The target shape exists in three places and should be the template rather than
being described abstractly. `src/engine/ExportJob.h` captures everything the
worker needs off the live state before the thread starts. `services/UpdateCheck.h`
is a type with `startCheck()` and `pump()`, its own worker, a deleted copy
constructor, a destructor that waits out the thread, public read-only fields for
the interface, and no back-pointer to the application. `Filmstrip` is a struct
with its own worker state.

Of the four proposed owners, three are substantially built. `RenderWorker`
already owns the thread, the schedule, the display buffers and
`builtPreviewMaxEdge_`. `Filmstrip` owns its entries and worker state.
`StatusSink` is `Workspace::status` plus a mutex and two accessors — twenty
lines, one reader. Only the session/document owner is genuinely missing. The
honest cost estimate is therefore "74 call sites rewritten across three existing
pieces plus one new type", not "a new architecture".

The best cohesion win is under-sold and is refereed.
`GuiState` holds deferred command state — `pendingOpenPath`, `showSidecarPrompt`,
`pendingWorkspaceDir`, `themeApplyPending`, `fontApplyPending` — meaning half of
the open-directory command's control flow lives in the UI state bag and is
drained by `drawUiFrame` at `UiFrame.cpp:487-495`. Moving open-flow state into
the document owner is a real reduction in reach, and `TestState.cpp:371-415`
asserts on those fields, so a wrong move fails the suite.

`Themes.cpp` at 786 lines is a data table, not a god object. File length is a
red herring there; no "split the large files" rider belongs in this plan.

## Goals vs. Non-Goals

**In scope**

- Remove the `Workspace` aggregate. Four owners, modelled on `UpdateCheck`:
  a document/open-flow owner, a render owner, `Filmstrip` as-is, and a
  `StatusSink`.
- Replace `RenderHost` and `WorkspaceRenderHost` with a per-request
  `RenderInputs` value, move the stop flag into the render owner, and pass a
  `StatusSink&` to the workers.
- Split `GuiState` into the six owners its fields indicate, and retarget
  `captureGui` and `applyGui` at six references.
- Move the five threading invariants into the types that own them, as acceptance
  criteria, with three new self-test cases for the seams the suite does not
  cover.
- Cut comments to a stated budget, keeping the seventeen-item do-not-touch list,
  with one mechanical CI gate.
- Rewrite structure-coupled tests before the code that they describe. Keep the
  eleven behaviour cases as they are and treat a failure in them as a defect.
- Rename `Workspace &app` into oblivion, plus `setStatus`/`getStatus`, `tex`,
  and decide on `doc` and `imgio`.
- Fix `Filmstrip.cpp`'s undeclared edges and add the include-direction gate so
  the new owners cannot silently reach upward.
- Four commits in one branch; call sites take a purpose-built struct at four or
  more owners, a plain reference below that.
- Update `PLAN.md` §Step 5, collapse `README.md:378-386` to a row.

**Out of scope**

- Any change to `settings.json`, `_directory.json` or the sidecar wire format.
  Key renames get their own branch with `version: 2` and a migration paragraph.
- Bumping to C++20. If the plan wants designated initializers, that is a
  separate decision with its own risk.
- Splitting `Themes.cpp` or other files on length alone.
- Encapsulating UI state behind setters. Fields stay reachable; methods cover
  operations.
- A headless ImGui smoke test. Optional, not required.
- New features, performance work, build-time work.

## Key Decisions & Staging for Next Steps

Resolved during ideation:

1. `RenderHost` is replaced by a per-request `RenderInputs` value — choice (b).
2. `GuiState` is split here, into the six owners named in the Analysis.
3. The preview buffer is owned by the render owner. `RenderInputs` reads it;
   only `rebuildPreview` writes it.
4. The format does not change. `PersistGui`, `PersistAppearance` and
   `PersistUpdates` keep their fields; only the two mapping functions change
   their parameters.
5. Tests: structure-coupled cases are rewritten before the code; the eleven
   behaviour cases are the referee and are not rewritten to match new output.

Still open, and needed before commit 3:

6. Who owns `chainOutputSpace`, `quit`, and the message sink after the split.
4. The new name for the document/open-flow owner, given `services/Session.h`
   occupies `Session`.
5. The exact struct threshold, stated as "four or more distinct owners".
6. The comment budget in lines, and the whitelist for the three-line gate.
7. Where each new type's translation unit lives, given the current edges — in
   particular whether `Filmstrip.cpp` moves to `services/` or `model/` gains an
   `io` edge.
8. Whether persistence changes at all in this branch. It does not.

Staging inside the one branch:

1. Comments and renames. No behavior. Independently revertable.
2. `StatusSink`, `chainOutputSpace` ownership, per-worker stop flags,
   `RenderInputs` replacing `RenderHost`, and the three new self-test cases.
   Green before the split starts. Choice (b) put the threading change here, which
   is why this commit is the one that must pass before commit 3 begins.
3. The component split — `Workspace` and its 74 call sites, `GuiState` and its
   252 field uses across six owners — plus the include-direction gate.
4. `PLAN.md` §Step 5, `README.md:378-386`, `docs/workspace-format.md` only if the
   format actually moved.

Before commit 3 begins, the branch should have a green commit 2 that the suite
has passed, because commit 3 is the only one that cannot be partially reverted.
