# Update Log

## 2026-10-02
- **Initialization**: Created the bundle root, the section index files, and this log.
- **Creation**: Added product concepts for the overview, macOS install, plugin discovery, image formats, and color management.
- **Creation**: Added architecture concepts for the module map, render pipeline, threading model, UI structure, OFX host contract, OFX suites, plugin loading, and Metal render.
- **Creation**: Added format concepts for the workspace file, the input sidecar, and the export sidecar.
- **Creation**: Added plugin concepts for the bundled Crop plugin and the tested third-party plugins.
- **Creation**: Added build concepts, including Attested Computations for `./build.sh` and `--selftest`.
- **Correction**: Corrected the Crop parameter list in `README.md`: the aspect option list, the crop amount, and the UI label.
- **Correction**: Removed the documentation-gap note from `plugins/crop.md` and recorded the exact option labels, defaults, and labels.
- **Correction**: Recorded the OFX default for `kOfxImageEffectPropCPURenderSupported` in `architecture/ofx-host-contract.md`.

## 2026-10-02 (abstraction refactor)

- **Context**: The refactor introduced `ParamType`, one image format table, `GlTexture`, `JsonValue`, `RenderSchedule`, `ChainRenderer`, `Effect::setInputSize`, and `NodeGraph::setNodeEnabled`. Only the concepts that stated a changed detail were revised.
- **Revision**: Aligned `architecture/render-pipeline.md` with `ChainRenderer`. One renderer per rendering thread owns the two chain buffers, in place of `thread_local` state. The export path now names `exporting`.
- **Revision**: Aligned `architecture/threading-model.md` with `RenderSchedule`. One type owns the render mutex, the condition variable, and the render flags. `RenderBusyGuard` and `ExportBusyGuard` became one `RenderSchedule::Guard`. Recorded the instance-lifetime Metal scratch buffers and the node enable and disable case. Moved the concurrency-case citation to `Selftest.cpp`.
- **Revision**: Aligned `architecture/metal-render.md`. The host no longer creates and releases the scratch buffers inside one render.
- **Revision**: Updated `architecture/module-map.md` for `src/selftest/`, `src/RenderSchedule.h`, the shared `JsonValue`, and `GlTexture`.
- **Correction**: Corrected `build/selftest.md`. The attester is `runSelfTest` in `src/selftest/Selftest.cpp`, not `selfTest` in `src/main.cpp`.
- **Revision**: Moved the input extension list citation in `product/image-formats.md` to `ImageLoad.cpp`, which now holds the single format table.
- **Revision**: Recorded the frame-loop thumbnail upload in `architecture/ui-structure.md`.
- **Correction**: Recorded the new bundle state in `index.md`. The refactor is uncommitted, so `v0.3.12` source URLs predate it.
- **Correction**: Pointed the Crop identity claim in `plugins/crop.md` at `src/selftest/Selftest.cpp`, which holds the Crop self-test.
- **Correction**: Pointed the self-test exclusion note in `plugins/third-party-plugins.md` at `src/selftest/Selftest.cpp`.
