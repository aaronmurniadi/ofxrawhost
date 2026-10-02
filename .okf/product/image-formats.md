---
type: Reference
title: Image formats
description: Accepted input formats, decoders, and export formats.
tags: [formats, imgio]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T16:45:00Z }
sources:
  - id: persist
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/persist/ProjectPersistSave.cpp
    title: ProjectPersistSave.cpp at v0.3.12
  - id: load
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageLoad.cpp
    title: ImageLoad.cpp at v0.3.12
  - id: write
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageWrite.cpp
    title: ImageWrite.cpp at v0.3.12
---

# Accepted input

The open dialog and the filmstrip accept these extensions.[^load]
One table in `ImageLoad.cpp` holds the list, so the dialog filter and the filmstrip tabs cannot drift apart.

| Group | Extensions |
| ----- | ---------- |
| Raster | `.png`, `.jpg`, `.jpeg` |
| TIFF | `.tif`, `.tiff` |
| EXR | `.exr` |
| RAW | `.cr2`, `.cr3`, `.nef`, `.arw`, `.dng`, `.raf`, `.orf`, `.rw2`, `.pef`, `.srw`, `.raw` |

# Decoders

| Format | Decoder |
| ------ | ------- |
| RAW | LibRaw, linear with camera white balance |
| TIFF | libtiff |
| PNG, JPEG | stb |
| EXR | tinyexr |

The TIFF path uses a scanline reader for non-tiled, contiguous, top-left-oriented files with one to four samples per pixel and one of these sample types: 8-bit integer, 16-bit integer, 16-bit float, 32-bit float.[^load]
Other TIFF files fall back to `TIFFReadRGBAImageOriented`, which yields 8-bit data.

RAW decode is serialized by one mutex.[^load]
Two thumbnail jobs therefore cannot decode RAW files at the same time.

# Output

The host exports PNG and JPEG only.[^write]
Both formats embed an ICC profile for the selected output tag.
The JPEG encoder uses the quality value from the GUI, which defaults to 92.
See [Color management](/product/color-management.md).

# Metadata files

The host hides its own `*.ofxrawhost.json` sidecars from the open dialog and the filmstrip.[^persist]
See [Input sidecar](/formats/input-sidecar.md).

[^persist]: ProjectPersistSave.cpp at v0.3.12
[^load]: ImageLoad.cpp at v0.3.12
[^write]: ImageWrite.cpp at v0.3.12
