# State files

OFX Raw Host persists processing state as JSON in its per-user state directory.
No file is written beside your images.

The state of a file or a directory holds the plugin chain and the color spaces
and nothing about the interface. The interface state, which is everything about
the theme, the font, the panels, the preview, and the export options, belongs to
the user and lives in the configuration directory above the state directory.

## State directory

Each entry mirrors the absolute path of the file it describes, so the name stays
readable and two files that share a name cannot collide:

```
{state}/Users/aaron/shots/
  photo.cr2.json          <- the chain of photo.cr2
  photo.cr2.export.json   <- how that export was made
  _directory.json         <- which image of the directory was open
```

`{state}` is `state` under the configuration directory.

## Directory entry

When you **Open Directory…** on a folder, the host reads and writes
`{state}/<mirrored folder>/_directory.json`:


```json
{
  "format": "ofxrawhost-workspace",
  "version": 1,
  "activeImage": "subfolder/DSC_0001.cr2"
}
```

Paths in `activeImage` are relative to the directory when possible.

## Per-user settings

The interface state lives in the per-user config directory:
`%APPDATA%\OfxRawHost` on Windows, `~/Library/Application Support/OfxRawHost` on
macOS, and `$XDG_CONFIG_HOME/ofxrawhost` (or `~/.config/ofxrawhost`) elsewhere.

The dock layout of the panels is an ImGui layout file, `layout.ini`, in that
directory. It is one file for the whole application, so opening a directory does
not change the layout.

The other preferences are in `settings.json` beside it:

```json
{
  "format": "ofxrawhost-settings",
  "version": 1,
  "gui": {
    "outputIndex": 0,
    "exportIndex": 0,
    "exportIntent": 0,
    "exportFormat": 1,
    "exportBitDepth": 8,
    "exportQuality": 92,
    "exportLossless": false,
    "exportSidecar": false,
    "previewRes": 0,
    "showPlugins": true,
    "showParameters": true,
    "showFilmstrip": false,
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

- `outputIndex` is the preview color space and `exportIndex` the export color
  space, both as an index into the color space list.
- `exportIntent` is the rendering intent, and `exportFormat`, `exportBitDepth`,
  `exportQuality`, and `exportLossless` are the export options.
- `exportSidecar` writes the record of an export, and it is off by default.
- `previewRes` is the preview size, and `showPlugins`, `showParameters`, and
  `showFilmstrip` are the panel visibility.
- `themeIndex` is the theme and `uiFontSizePt` the interface font size in
  points, where 0 follows the system font size.
- `channel` is 0 for stable releases and 1 for pre-releases; a release build
  always checks stable releases. `devWarningShown` records that a development
  build has already offered to turn the start-up check off.

Every one of these belongs to the user, so a directory entry and a sidecar carry
none of them, and opening a directory or an image never changes them.

## Input sidecar

For each source file `photo.cr2`, the chain is stored in
`{state}/<mirrored path>/photo.cr2.json`. The host writes it when the image
changes or when the application exits, and reads it when the image opens.

## Export sidecar

An export writes `{state}/<mirrored path>/<export name>.json` only while **Export
sidecar** is on in the export dialog. The toggle is a per-user setting, off by
default, and it applies to the next export.

An export sidecar records the full chain, parameters, input path, and color
space, so the render can be reproduced or audited later. `inputColorSpace` is the
space the file declares; `chainInputColorSpace` is the space the user picked for
the chain, which the loader hands to the first plugin.

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

`version` is incremented when incompatible changes are made. The host ignores
unknown keys and fills missing fields with defaults.

## Files from an older build

A build before the state moved wrote `{image}.ofxrawhost.json` beside the image,
`{export}.json` beside the export, and `{folder}/workspace.ofxrawhost.json`
inside the directory. The host still reads those three when the matching state
entry is missing, and it never writes them again. Delete them once the state
directory holds the entry.
