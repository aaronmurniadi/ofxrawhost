---
type: Product Overview
title: OFX Raw Host overview
description: What OFX Raw Host is and what it does.
tags: [ofx, product]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
  - id: host-suites
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxSuites.cpp
    title: OfxSuites.cpp at v0.3.12
---

# Overview

OFX Raw Host is a minimal still-image OpenFX plugin host.[^readme]
It opens a RAW or raster photo, runs the photo through a chain of OFX filter plugins, shows a preview, and exports PNG or JPEG.

The host targets users who want to try OFX effects without a video NLE.
Such effects normally run only inside Resolve, Nuke, or a similar host.

## What the host does

- Decodes RAW, TIFF, PNG, JPEG, and EXR input. See [Image formats](/product/image-formats.md).
- Runs any OFX plugin that declares the filter context. See [Plugin loading](/architecture/plugin-loading.md).
- Chains plugins and supports reorder and bypass.
- Previews the chain with zoom, pan, and four resolution caps.
- Exports full-resolution PNG or JPEG with an ICC profile.
- Persists reproducible state as JSON. See [Formats](/formats/index.md).

## What the host does not do

- The host processes still images only.
- The host has no timeline and no keyframe animation.
- The host does not composite more than one input.

## Bundled and tested plugins

The app ships the [Crop plugin](/plugins/crop.md).
Crop sits at the front of a chain and reports a smaller region of definition.

Plugins that the author tested appear in [Tested third-party plugins](/plugins/third-party-plugins.md).

## Version

The current repository tag is `v0.3.12`.
The host reports version 0.3.12 and OpenFX API version 1.4 to plugins.[^host-suites]

[^readme]: README.md at v0.3.12
[^host-suites]: OfxSuites.cpp at v0.3.12
