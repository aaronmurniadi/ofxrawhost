---
type: Architecture
title: Module map
description: Source layout, module responsibilities, and build targets.
tags: [architecture, source]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T16:45:00Z }
sources:
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
---

# Directory layout

| Path | Responsibility |
| ---- | -------------- |
| `src/main.cpp` | Entry point and the `--selftest` argument |
| `src/selftest/` | Self-test cases for decode, color, Crop, every plugin, and concurrency |
| `src/AppState.h` | `App` state, `Node`, filmstrip entries, and the enums |
| `src/RenderSchedule.h` | Render mutex, condition variable, and request flags |
| `src/Actions.cpp` | Open image, open workspace, export, and sidecar writes |
| `src/NodeGraph.cpp` | Chain mutation, parameter changes, and chain capture and apply |
| `src/RenderPipeline.cpp` | Render worker, preview rebuild, and display upload |
| `src/UI.cpp` | GLFW and ImGui application loop |
| `src/Filmstrip.cpp` | Thumbnail queue and thumbnail worker |
| `src/perf.h` | Stage timing that logs to stderr |
| `src/imgio/` | Decode, preview downscale, color conversion, and export |
| `src/ofx/` | Host properties, suites, plugin loading, CPU and Metal render |
| `src/persist/` | Workspace file, sidecar load and save, and the shared `JsonValue` |
| `src/ui/` | Dock panels, widgets, themes, ImGui backend, GL textures, and macOS pinch |
| `plugins/Crop.cpp` | Bundled OFX plugin |
| `third_party/` | OpenFX SDK submodule, stb, tinyexr, dialogs, and icons |

# Dependency direction

The `src/ui/` modules call `Actions`, `NodeGraph`, and `RenderPipeline`.[^cmake]
Those modules call `imgio`, `ofx`, and `persist`.

# Build targets

CMake defines one executable, two libraries, and one module:[^cmake]

| Target | Kind | Purpose |
| ------ | ---- | ------- |
| `OfxRawHost` | executable | The host |
| `imgui_lib` | static library | Dear ImGui with the GLFW and OpenGL3 backends |
| `OfxSupport` | static library | OpenFX support library from the submodule |
| `OfxRawHostCrop` | module | Bundled plugin, output name `OfxRawHost.ofx` |

The `app` target assembles `build/OfxRawHost.app` on macOS.[^cmake]
A custom command assembles `OfxRawHost.ofx.bundle` under `build/Plugins`.

# Third-party sources

GLFW 3.4 and Dear ImGui `v1.91.8-docking` arrive through `FetchContent`.[^cmake]
LibRaw, libtiff, Little CMS 2, and zlib are system dependencies.
See [Build dependencies](/build/dependencies.md).

[^cmake]: CMakeLists.txt at v0.3.12
