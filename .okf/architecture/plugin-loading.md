---
type: Behavior
title: Plugin loading
description: How the host finds, validates, and instantiates OFX plugin bundles.
tags: [ofx, plugins, architecture]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
---

# Bundle layout

The host scans a directory tree for entries named `*.ofx.bundle`.[^plugins]
Inside a bundle the host looks for `Contents/<arch>/<stem>`.
The stem is the bundle name without `.ofx.bundle`.

| Platform | Architecture directory |
| -------- | ---------------------- |
| macOS | `MacOS` |
| Linux x86-64 | `Linux-x86-64` |
| Linux arm64 | `Linux-arm-64` |
| Linux 32-bit | `Linux-x86` |
| Windows 64-bit | `Win64` |
| Windows 32-bit | `Win32` |

On macOS the host also tries `MacOS/arm64`, `MacOS/x86_64`, and `MacOS/universal`.

The build places the bundled plugin at `build/Plugins/OfxRawHost.ofx.bundle/Contents/<arch>/OfxRawHost.ofx`.[^cmake]
The binary name must match the bundle stem.

# Load sequence

The host processes each bundle as follows:[^plugins]

1. Load the dynamic library with `dlopen` or `LoadLibraryW`.
2. Call `OfxSetHost` when the library exports it.
3. Read `OfxGetNumberOfPlugins` and `OfxGetPlugin`, then call `setHost` on each plugin.
4. Keep the plugins whose `pluginApi` matches `kOfxImageEffectPluginApi`.
5. Call `kOfxActionLoad`, then `kOfxActionDescribe`.
6. Require the filter context in `kOfxImageEffectPropSupportedContexts`.
7. Call `kOfxImageEffectActionDescribeInContext` with the filter context.

A failure at any step drops the plugin.
The host prints a skip message when the library fails to load.

# Labels and order

The plugin label comes from `kOfxPropLabel`, with the plugin identifier as a fallback.[^plugins]
The author group comes from `kOfxImageEffectPluginPropGrouping`.
Without a grouping the host uses the first two dot-separated segments of the plugin identifier.

The host sorts the plugin list by author, then by label.

# Instantiation

`createInstance` clones the descriptor, sets the instance properties, and resets every parameter to its default.[^plugins]
The host then calls `kOfxActionCreateInstance`.
A failed create returns a null handle to the caller.

# Metal capability

A plugin declares GPU render with `kOfxImageEffectPropMetalRenderSupported`.[^plugins]
The host records the flag and logs a message for every capable plugin.
See [Metal render](/architecture/metal-render.md).

[^plugins]: OfxPlugins.cpp at v0.3.12
[^cmake]: CMakeLists.txt at v0.3.12
