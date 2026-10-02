---
type: OFX Plugin
title: Crop plugin
description: Bundled crop plugin with aspect, crop amount, and pan parameters.
tags: [ofx, plugins, bundled]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: crop
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/plugins/Crop.cpp
    title: Crop.cpp at v0.3.12
  - id: main
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/main.cpp
    title: main.cpp at v0.3.12
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
---

# Identity

| Property | Value |
| -------- | ----- |
| Identifier | `com.aaronmurniadi.ofxrawhost.crop` |
| Label | Crop |
| Grouping | OFX Raw Host |
| Version | 1.0 |
| Context | filter |

# Purpose

Crop reduces the image to an aspect-ratio window.[^crop]
The plugin overrides its region of definition, so the host allocates a smaller output buffer.
Downstream plugins therefore process fewer pixels.
Place Crop at the front of a chain for the best effect.[^readme]

At the default parameters the region of definition equals the source size and the render is an identity pass-through.[^main]

# Parameters

| Parameter | UI label | Type | Range | Meaning |
| --------- | -------- | ---- | ----- | ------- |
| `aspect` | Aspect Ratio | choice | 12 entries | Target aspect ratio; default Original |
| `crop` | Crop | double | 0 to 100 | Crop amount; default 0 |
| `offsetX` | Offset X | double | -100 to 100 | Horizontal pan of the crop center; default 0 |
| `offsetY` | Offset Y | double | -100 to 100 | Vertical pan of the crop center; default 0 |

The aspect options keep this order: Original, 1:1 (Square), 4:5 (Portrait), 3:4 (Portrait), 9:16 (Vertical Video), 16:9 (Widescreen), 4:3 (Classic TV), 3:2 (Film Landscape), 2:3 (Film Portrait), 1.85:1 (Cinema Flat), 2.39:1 (Anamorphic), and 21:9 (Ultrawide).[^crop]

# Labels

The host displays `kOfxPropLabel` for each parameter.[^crop]
The plugin sets that property through `setLabels`, so the Aspect parameter appears as **Aspect Ratio** and not as **Aspect**.
Each numeric parameter uses an increment of 1.

# Crop amount

The plugin scales the largest fitting rectangle by `max(1 - crop/100, 0.02)`.[^crop]
The floor of 0.02 keeps the window non-degenerate.
At 100 the window is 2 percent of the fitted rectangle in each dimension, which is 0.04 percent of its area.

# Pan and black fill

The offset range follows the crop window.[^crop]
When the window is smaller than the source in a dimension, the range is the available slack.
When the window fills or exceeds the source, the range becomes half the crop size.
The window can then slide past the source edge, and out-of-bounds areas render as black.

Without this rule a zero offset range would make the sliders ineffective at a full-size crop.

# Build

The plugin links the OpenFX support library.[^cmake]
The build names the binary `OfxRawHost.ofx` and assembles `OfxRawHost.ofx.bundle`.
See [Plugin loading](/architecture/plugin-loading.md).

See [Plugin loading](/architecture/plugin-loading.md).

[^crop]: Crop.cpp at v0.3.12
[^main]: main.cpp at v0.3.12
[^cmake]: CMakeLists.txt at v0.3.12
[^readme]: README.md at v0.3.12
