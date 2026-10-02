---
type: Data Format
title: Input sidecar
description: Per-image plugin chain and parameters beside the image.
tags: [formats, persist, sidecar]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: docs
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/docs/workspace-format.md
    title: workspace-format.md at v0.3.12
  - id: save
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/persist/ProjectPersistSave.cpp
    title: ProjectPersistSave.cpp at v0.3.12
  - id: actions
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/Actions.cpp
    title: Actions.cpp at v0.3.12
---

# Path

For a source file `photo.cr2` the host writes `photo.cr2.ofxrawhost.json`.[^docs]
The host appends `.ofxrawhost.json` to the full image path, so the scheme works for every extension.[^save]

# Schema

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `format` | string | `ofxrawhost-sidecar` |
| `version` | integer | 1 |
| `kind` | string | `input` |
| `sourcePath` | string | File name of the image |
| `inputColorSpace` | string | Detected input tag |
| `gui` | object | UI state at save time |
| `chain` | object | Plugin chain |

# Chain object

`chain.selectedNode` holds the selected index.[^save]
`chain.nodes` is a list of node objects with these fields:

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `pluginIdentifier` | string | OFX plugin identifier |
| `pluginLabel` | string | Display label |
| `enabled` | boolean | Bypass state |
| `groupOpen` | object | Expanded state per parameter group |
| `params` | object | Parameter values as raw JSON per parameter name |

A parameter value keeps its JSON type: number, boolean, string, or array.[^save]
A preset therefore round-trips without a type conversion.

# Write points

The host writes the sidecar when another image opens, when a workspace opens, and on an explicit save.[^actions]
See [Workspace file](/formats/workspace-file.md) and [Export sidecar](/formats/export-sidecar.md).

# Not an image

The host rejects a sidecar path as an image input and reports a status message instead.[^actions]
The open dialog and the filmstrip skip sidecar files.[^save]

[^docs]: workspace-format.md at v0.3.12
[^save]: ProjectPersistSave.cpp at v0.3.12
[^actions]: Actions.cpp at v0.3.12
