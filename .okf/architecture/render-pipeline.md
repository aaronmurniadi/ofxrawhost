---
type: Architecture
title: Render pipeline
description: The path from a decoded image to the preview texture and the export file.
tags: [architecture, render, concurrency]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: render
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/RenderPipeline.cpp
    title: RenderPipeline.cpp at v0.3.12
  - id: appstate
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/AppState.h
    title: AppState.h at v0.3.12
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: actions
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/Actions.cpp
    title: Actions.cpp at v0.3.12
---

# Stages

1. `loadImage` decodes the file into `app.full`. See [Image formats](/product/image-formats.md).
2. `rebuildPreview` waits for render idle, downscales `app.full` into `app.preview`, and schedules a render.
3. The render worker calls `renderChain` for the preview.
4. `renderChain` applies each enabled node in order and calls the plugin render action.
5. `publishDisplay` converts the result to sRGB 8-bit RGBA and marks the display dirty.
6. `pumpDisplayUpload` uploads the texture on the UI thread.

# Chain execution

`renderChain` keeps two thread-local buffers, `cur` and `next`.[^render]
The buffers grow when a plugin reports a larger region of definition.
The buffers never shrink, so a large intermediate result stays allocated.

Each node queries its output size with `kOfxImageEffectActionGetRegionOfDefinition` before the render.[^plugins]
A plugin that does not override the region of definition returns the input size.
The [Crop plugin](/plugins/crop.md) reduces the size, so later plugins process fewer pixels.

The chain skips a disabled node.
An empty chain or a fully bypassed chain returns the source unchanged.

# Generation counter

`gLatestGen` invalidates work that is already in flight.[^render]
An interactive render passes its generation to `renderEffect`.
`renderChain` stops when the generation changes.
An export passes generation 0, so an export never aborts.

# Preview resolution

The Preview res combo caps the long edge of the preview:[^appstate]

| Label | Long-edge cap |
| ----- | ------------- |
| 720p | 1280 |
| 1080p | 1920 |
| 1440p | 2560 |
| Full res | none |

# Display path

The display buffer holds bottom-up float RGBA.[^appstate]
`toDisplayRGBA8` converts the buffer into top-down sRGB 8-bit RGBA with Little CMS 2.
The worker sets `displayDirty`.
The UI thread then uploads the texture.

When only the output tag changes, the worker recolors the cached display buffer instead of rendering again.[^render]
`scheduleDisplayRecolor` marks that case.

# Export path

`doExport` appends `.png` or `.jpg` when the destination path has no extension.[^actions]
The export renders `app.full` at full resolution on a detached thread.
The thread sets `exportBusy`, so the worker waits and no second render overlaps.
The export writes the [export sidecar](/formats/export-sidecar.md) after a successful write.

The status bar reports the output dimensions of the preview render, or the OFX status when the render fails.

[^render]: RenderPipeline.cpp at v0.3.12
[^appstate]: AppState.h at v0.3.12
[^plugins]: OfxPlugins.cpp at v0.3.12
[^actions]: Actions.cpp at v0.3.12
