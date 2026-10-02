---
type: Behavior
title: Plugin discovery
description: Plugin directories, OFX_PLUGIN_PATH, and the bundled plugin.
tags: [ofx, plugins, install]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
---

# Search order

The host collects plugin directories in this order:[^plugins]

1. Each entry of the `OFX_PLUGIN_PATH` environment variable.
2. The platform default directory.
3. The bundled plugin directories next to the executable.

| Platform | Default directory |
| -------- | ----------------- |
| macOS | `/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins` and `/usr/local/OFX/Plugins` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins` |

`OFX_PLUGIN_PATH` uses `:` as the separator. Windows uses `;`.

The host also reads `<exe>/Plugins` and `<exe>/../PlugIns`.[^plugins]
The macOS app bundle keeps the bundled plugin in `Contents/PlugIns`.

# Install a plugin

Install plugins the way the author recommends.
You can also copy a `*.ofx.bundle` into the default directory.[^readme]

An app started from Finder does not see shell variables.
Start the app from a terminal to use `OFX_PLUGIN_PATH`:

```sh
OFX_PLUGIN_PATH=/path/to/plugins /Applications/OfxRawHost.app/Contents/MacOS/OfxRawHost
```

Restart the host after you install a plugin.
The status bar reports when the host finds no plugin.

# Filter context only

The host keeps plugins that declare the filter context (`kOfxImageEffectContextFilter`).[^plugins]
The host skips plugins that declare other contexts only.
See [Plugin loading](/architecture/plugin-loading.md).

[^plugins]: OfxPlugins.cpp at v0.3.12
[^readme]: README.md at v0.3.12
