---
type: Data Format
title: Workspace file
description: Folder-level UI state in workspace.ofxrawhost.json.
tags: [formats, persist, workspace]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: docs
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/docs/workspace-format.md
    title: workspace-format.md at v0.3.12
  - id: save
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/persist/ProjectPersistSave.cpp
    title: ProjectPersistSave.cpp at v0.3.12
---

# Path

The host reads and writes `{folder}/workspace.ofxrawhost.json` for a workspace folder.[^docs]

Dock geometry lives in a separate file, `{folder}/.ofxrawhost-layout.ini`.
See [UI structure](/architecture/ui-structure.md).

# Schema

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `format` | string | `ofxrawhost-workspace` |
| `version` | integer | 1 |
| `activeImage` | string | Active image, relative to the workspace directory when possible |
| `gui` | object | Folder-level UI state |

The `gui` object holds these fields:[^save]

| Field | Default |
| ----- | ------- |
| `outputIndex` | 0 |
| `exportFormat` | 1 |
| `jpegQuality` | 92 |
| `previewRes` | 1 |
| `themeIndex` | 2 |
| `showLeft` | true |
| `showRight` | true |
| `showFilmstrip` | true |

# Example

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

# Scope

The workspace file holds folder-level state only.[^docs]
A per-image plugin chain lives in the [input sidecar](/formats/input-sidecar.md).

# Legacy fields

An older file may still hold `leftW`, `rightW`, and `filmstripH` under `gui`.[^docs]
The host reads those values to seed a default layout when no layout file exists.
The host does not write them again.

[^docs]: workspace-format.md at v0.3.12
[^save]: ProjectPersistSave.cpp at v0.3.12
