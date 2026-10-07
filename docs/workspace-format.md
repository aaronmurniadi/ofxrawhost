# Workspace and sidecar JSON

OFX Raw Host persists reproducible processing state as JSON beside your files.

## Workspace file

When you **Open Workspace…** on a folder, the host reads and writes:

`{folder}/workspace.ofxrawhost.json`

This file captures folder-level UI state and which image is active. Per-image OFX chains live in sidecars (below), not in the workspace file.

Dock panel sizes and splits are stored separately in ImGui’s layout file:

`{folder}/.ofxrawhost-layout.ini`

(without a workspace, the app uses `ofxrawhost.ini` in the working directory).

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
    "themeIndex": 2,
    "showLeft": true,
    "showRight": true,
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

Export sidecars record the full chain, parameters, input path, output color space, and export options so the render can be reproduced or audited later.

## Sidecar document shape

Input and export sidecars share the same `sidecar` object:

```json
{
  "format": "ofxrawhost-sidecar",
  "version": 1,
  "kind": "input",
  "sourcePath": "DSC_0001.cr2",
  "inputColorSpace": "Linear Rec.2020",
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
