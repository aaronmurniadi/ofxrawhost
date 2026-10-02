---
type: Data Format
title: Export sidecar
description: Audit record of one export beside the exported file.
tags: [formats, persist, export]
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

The host writes the sidecar next to the exported file, with the same stem and a `.json` extension.[^save]

| Export | Sidecar |
| ------ | ------- |
| `img-a7Kx.jpg` | `img-a7Kx.json` |
| `out/export.png` | `out/export.json` |

# Schema

The export sidecar repeats the [workspace](/formats/workspace-file.md) `gui` block and the full [chain](/formats/input-sidecar.md), and adds these fields:[^save]

| Field | Type | Meaning |
| ----- | ---- | ------- |
| `format` | string | `ofxrawhost-sidecar` |
| `version` | integer | 1 |
| `kind` | string | `export` |
| `sourcePath` | string | Full path of the input image |
| `inputColorSpace` | string | Detected input tag |
| `exportedAt` | string | ISO 8601 timestamp |

# Purpose

The sidecar records the full chain, the parameter values, the input path, the input color space, and the export options.[^docs]
A later run can reproduce or audit the render.

# Write conditions

The host writes the sidecar only after a successful encode and an OK OpenFX status.[^actions]
When the destination path has no extension, the host appends `.png` or `.jpg` from the export format first.[^actions]

[^docs]: workspace-format.md at v0.3.12
[^save]: ProjectPersistSave.cpp at v0.3.12
[^actions]: Actions.cpp at v0.3.12
