---
type: Reference
title: Color management
description: Output tags, input color detection, and ICC handling.
tags: [color, imgio]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: appstate
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/AppState.h
    title: AppState.h at v0.3.12
  - id: imageio
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageIO.h
    title: ImageIO.h at v0.3.12
  - id: load
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageLoad.cpp
    title: ImageLoad.cpp at v0.3.12
  - id: color
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageColor.cpp
    title: ImageColor.cpp at v0.3.12
---

# Output tag

The Output tag combo offers four spaces:[^appstate]

| Index | Tag | Use |
| ----- | --- | --- |
| 0 | sRGB | Standard display output |
| 1 | Display P3 | Wide-gamut display output |
| 2 | Linear Rec.709 | Linear working space |
| 3 | Linear Rec.2020 | Wide linear working space |

Plugins receive pixels in the selected output tag space.
The host assumes that plugin output is already in that space.[^imageio]
The host only embeds the matching ICC profile in the export.

# Input detection

The host infers the input color space at load time.[^load]

| Input | Detected space |
| ----- | -------------- |
| RAW | Linear Rec.2020 |
| EXR | Linear Rec.2020 |
| TIFF, PNG, or JPEG with an ICC profile | Nearest of the four tags |
| Untagged float TIFF | Linear Rec.2020 |
| Untagged integer TIFF, PNG, or JPEG | sRGB |

ICC classification reads the profile description first, then compares primaries and the tone curve.[^color]
A description that contains `prophoto`, `rec2020`, `bt2020`, or `display p3` maps to a wide space.

# Display conversion

Display buffers stay scene-linear on the plugin side.[^appstate]
For an unprocessed source the host selects the linear counterpart of the file tag.
`Display P3` has no linear tag, so the host uses Linear Rec.2020 as the nearest wider linear space.

The host converts to sRGB 8-bit RGBA for texture upload with Little CMS 2.[^imageio]
Profiles and CMS transforms are cached, because the conversion runs for every frame.[^color]

[^appstate]: AppState.h at v0.3.12
[^imageio]: ImageIO.h at v0.3.12
[^load]: ImageLoad.cpp at v0.3.12
[^color]: ImageColor.cpp at v0.3.12
