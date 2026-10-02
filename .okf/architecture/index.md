# Architecture

- [Module map](./module-map.md) - Source layout, module responsibilities, and build targets
- [Render pipeline](./render-pipeline.md) - The path from a decoded image to the preview texture and the export file
- [Threading model](./threading-model.md) - Threads, mutexes, and generation counters that keep renders race-free
- [UI structure](./ui-structure.md) - Dock panels, layout persistence, themes, preview navigation, and filmstrip thumbnails
- [OFX host contract](./ofx-host-contract.md) - Host properties and capabilities that OFX plugins can query
- [OFX suites](./ofx-suites.md) - The six OFX suites the host serves and the functions in each
- [Plugin loading](./plugin-loading.md) - How the host finds, validates, and instantiates OFX plugin bundles
- [Metal render](./metal-render.md) - GPU render path for Apple systems and the `OFX_HOST_METAL` switch
