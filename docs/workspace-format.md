# Workspace and sidecar JSON

OFX Raw Host persists reproducible processing state as JSON beside your files.

## Workspace file

When you **Open Directory…** on a folder, the host reads and writes:

`{folder}/workspace.ofxrawhost.json`

This file captures folder-level UI state and which image is active. Per-image OFX chains live in sidecars (below), not in the workspace file.

Dock panel sizes and splits are stored separately in ImGui’s layout file:

`{folder}/.ofxrawhost-layout.ini`

(without a workspace, the app uses `layout.ini` in the per-user config directory).

Preferences that outlive a workspace live in `settings.json` in the same
per-user config directory: `%APPDATA%\OfxRawHost` on Windows,
`~/Library/Application Support/OfxRawHost` on macOS, and
`$XDG_CONFIG_HOME/ofxrawhost` (or `~/.config/ofxrawhost`) elsewhere. It uses the
same `gui` object as a workspace project, plus the theme, the interface font
size, and an `updates` object:

```json
{
  "format": "ofxrawhost-settings",
  "version": 1,
  "gui": {
    "...": "same keys as workspace gui block",
    "themeIndex": 2,
    "uiFontSizePt": 0
  },
  "updates": {
    "channel": 1,
    "checkAtStartup": true,
    "devWarningShown": false
  }
}
```

`themeIndex` is the theme of the interface and `uiFontSizePt` is its size in
points, where 0 follows the system font size. The theme and the font size belong
to the user, so they are read and written here alone: a workspace project and a
sidecar never carry them, and opening either one never changes them.

`channel` is 0 for stable releases and 1 for pre-releases; a release build
always checks stable releases. `devWarningShown` records that a development
build has already offered to turn the start-up check off. Update preferences live
only here, so opening an image with a sidecar never changes them.

A workspace project holds the same `gui` object plus the active image, and its
`gui` object omits the theme and the interface font size, which stay in the
per-user settings file:

```json
{
  "format": "ofxrawhost-workspace",
  "version": 1,
  "activeImage": "subfolder/DSC_0001.cr2",
  "gui": {
    "outputIndex": 0,
    "exportFormat": 1,
    "jpegQuality": 92,
    "previewRes": 1,
    "showPlugins": true,
    "showParameters": true,
    "showFilmstrip": true
  }
}
```

Paths in `activeImage` are relative to the workspace directory when possible.

Panel geometry is not stored here or in the sidecars; ImGui's layout `.ini` owns it.

## Input image sidecar

For each source file `photo.cr2`, settings are stored in:

`photo.cr2.ofxrawhost.json`

Same basename as the image, with `.ofxrawhost.json` appended (works for any extension).

## Export sidecar

On export, a sibling JSON is written next to the image file:

| Export           | Sidecar           |
| ---------------- | ----------------- |
| `img-a7Kx.jpg`   | `img-a7Kx.json`   |
| `out/export.png` | `out/export.json` |

Export sidecars record the full chain, parameters, input path, output color space, and export options so the render can be reproduced or audited later. `inputColorSpace` is the space the file declares; `chainInputColorSpace` is the space the user picked for the chain, which the loader hands to the first plugin.

## Sidecar document shape

Input and export sidecars share the same `sidecar` object:

```json
{
  "format": "ofxrawhost-sidecar",
  "version": 1,
  "kind": "input",
  "sourcePath": "DSC_0001.cr2",
  "inputColorSpace": "Linear Rec.709",
  "chainInputColorSpace": "Linear Rec.709",
  "exportedAt": "2026-09-27T12:00:00Z",
  "gui": { "...": "same keys as workspace gui block" },
  "chain": {
    "selectedNode": 0,
    "nodes": [
      {
        "pluginIdentifier": "com.example.filter",
        "pluginLabel": "Example Filter",
        "enabled": true,
        "groupOpen": { "Grain": true },
        "params": {
          "amount": 0.5,
          "mode": 2,
          "labelParam": "text",
          "toggle": true
        }
      }
    ]
  }
}
```

- `kind`: `"input"` for image sidecars, `"export"` for export sidecars.
- `params`: keyed by OFX parameter **name** (stable). Values are numbers, booleans, or strings depending on type; multi-dimensional params are JSON arrays.
- `pluginIdentifier`: used to rebind plugins if install order changes; `pluginLabel` is informational.

## Versioning

`version` is incremented when incompatible changes are made. The host ignores unknown keys and fills missing fields with defaults.
