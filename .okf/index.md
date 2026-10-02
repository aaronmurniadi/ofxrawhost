---
okf_version: "0.2"
---

# OFX Raw Host knowledge bundle

Knowledge bundle for OFX Raw Host, a minimal still-image OpenFX plugin host.

The host opens a RAW or raster photo, runs the photo through a chain of OFX filter plugins, shows a preview, and exports PNG or JPEG.

## Sections

- [Product](/product/index.md) - Overview, install steps, plugin discovery, image formats, and color management
- [Architecture](/architecture/index.md) - Module map, render pipeline, threading model, UI structure, OFX contract, plugin loading, and Metal render
- [Formats](/formats/index.md) - Workspace file, input sidecar, and export sidecar
- [Plugins](/plugins/index.md) - Bundled Crop plugin and tested third-party plugins
- [Build](/build/index.md) - Dependencies, build and self-test computations, and release process

## Bundle state

Every concept carries `status: draft` and no `verified` entry.
The content is agent-generated.
The architecture, product, and build concepts describe the working tree after the abstraction refactor.
That refactor is not committed, so source URLs that cite tag `v0.3.12` predate it.
