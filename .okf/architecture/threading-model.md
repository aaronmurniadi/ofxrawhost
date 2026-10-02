---
type: Invariant
title: Threading model
description: Threads, mutexes, and generation counters that keep renders race-free.
tags: [architecture, concurrency, invariants]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T16:45:00Z }
sources:
  - id: appstate
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/AppState.h
    title: AppState.h at v0.3.12
  - id: schedule
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/main/src/RenderSchedule.h
    title: RenderSchedule.h
  - id: render
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/RenderPipeline.cpp
    title: RenderPipeline.cpp at v0.3.12
  - id: host
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxHost.h
    title: OfxHost.h at v0.3.12
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
  - id: selftest
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/main/src/selftest/Selftest.cpp
    title: Selftest.cpp
---

# Threads

| Thread | Role |
| ------ | ---- |
| UI thread | GLFW events, ImGui frame, and texture upload |
| Render worker | Preview renders and display recolor |
| Thumbnail worker | Filmstrip thumbnails |
| Export thread | One detached full-resolution export |
| OFX worker pool | Parallel slices that a plugin requests through the multi-thread suite |

# Render state

`RenderSchedule` owns one mutex, one condition variable, and three flags: `busy`, `exporting`, and `recolorPending`.[^schedule]
The mutex guards those three flags.
`pending` is a separate atomic, so the worker can test an outstanding request without the lock.
`RenderSchedule::Guard` sets `busy` or `exporting` under the mutex and clears it with a notify on the way out.
The render worker and the export thread both use that guard.
`waitRenderIdle` calls `RenderSchedule::waitIdle`, which clears the pending flags and waits until no render and no export is active.

Call `waitRenderIdle` before any operation that rewrites state the worker reads.[^render]
Such operations are preview rebuilds, node graph mutations, node enable and disable, image swaps, and exports.

# Generation guard

Every schedule call increments `gLatestGen`.[^render]
A render compares its generation against `gLatestGen` after each node.
A mismatch aborts the chain, so a stale result never reaches the display.

# Per-object locks

| Lock | Guards | Reason |
| ---- | ------ | ------ |
| `displayMutex` | `display`, `displayRGBA`, `displayDirty` | The worker writes; the UI thread uploads |
| `Effect::dimMutex` | `w`, `h`, `outW`, `outH` | Renders write; suite actions read |
| `gValueMutex` | Parameter values | The UI writes while renders read |
| `statusMutex` | Status string | Any thread writes |
| `thumbMutex` | Thumbnail queue | The UI thread and the thumbnail worker |

`app.outputTag` is an `std::atomic<ColorSpace>`, because the UI thread writes it while renders run.[^appstate]

Clip region-of-definition reads happen through the suite on the UI thread.
The render writes the same dimensions.
`dimMutex` therefore covers both sides.[^host][^plugins]

An `Effect` keeps its Metal scratch buffers, `srcMtl` and `dstMtl`, for the life of the instance.[^plugins]
A buffer grows when a larger one is needed, and is otherwise reused.
`Effect::~Effect` releases them.

# OpenFX support library patch

The OpenFX support library keeps a process-global log indent counter.[^cmake]
Action dispatches on the render thread and the UI thread race on that counter.
The build copies `ofxsLog.cpp` and changes `gIndent` to `thread_local`.
The build fails when the patch anchor is missing.

# Verification

The `--selftest` concurrency case exercises the worker, graph mutation, and an export at the same time.[^selftest]
See [Run the self-test](/build/selftest.md).

[^appstate]: AppState.h at v0.3.12
[^schedule]: RenderSchedule.h
[^render]: RenderPipeline.cpp at v0.3.12
[^host]: OfxHost.h at v0.3.12
[^plugins]: OfxPlugins.cpp at v0.3.12
[^cmake]: CMakeLists.txt at v0.3.12
[^selftest]: Selftest.cpp
