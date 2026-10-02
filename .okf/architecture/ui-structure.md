---
type: Architecture
title: UI structure
description: Dock panels, layout persistence, themes, preview navigation, and filmstrip thumbnails.
tags: [architecture, ui]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T16:45:00Z }
sources:
  - id: dock
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ui/DockLayout.h
    title: DockLayout.h at v0.3.12
  - id: backend
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ui/ImGuiBackend.cpp
    title: ImGuiBackend.cpp at v0.3.12
  - id: filmstrip
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/Filmstrip.h
    title: Filmstrip.h at v0.3.12
  - id: load
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/imgio/ImageLoad.cpp
    title: ImageLoad.cpp at v0.3.12
  - id: docs
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/docs/workspace-format.md
    title: workspace-format.md at v0.3.12
  - id: appstate
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/AppState.h
    title: AppState.h at v0.3.12
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
---

# Backend

The UI uses GLFW with the Dear ImGui OpenGL3 backend.[^backend]
The ImGui docking branch provides the dock space.
Font Awesome solid icons come from `third_party/fontawesome`.[^cmake]
The build copies `fa-solid-900.ttf` next to the binary.

# Panels

The default layout defines four docks:[^dock]

| Panel | Content |
| ----- | ------- |
| OFX Plugins | Installed plugins, with a label filter |
| Preview | Rendered image, with zoom and pan |
| OFX Parameters | Parameters of the selected node, with a name filter |
| Filmstrip | Images of the workspace, in the tabs All, RAW, and Compressed |

# Layout persistence

ImGui stores the dock layout in `{workspace}/.ofxrawhost-layout.ini`.[^backend]
Without a workspace the app uses `ofxrawhost.ini` in the working directory.

An older workspace file may still carry `leftW`, `rightW`, and `filmstripH`.[^docs]
The host reads those values once to seed a default layout when no layout file exists.
The [workspace file](/formats/workspace-file.md) no longer writes them.

# Themes

The theme combo selects a built-in theme set from ImThemes.[^appstate]
The default index is 2, which is the Photoshop theme.

# Preview navigation

A zoom factor of 1 fits the image to the view.[^appstate]
The host resets the zoom factor and the pan offsets when a new image opens.
A pinch gesture on macOS is handled in `MacPinch.m`.[^cmake]

# Filmstrip

The filmstrip snaps the thumbnail resolution to this ladder: 16, 24, 32, 48, 256, and 1024 pixels.[^filmstrip]
A thumbnail job renders at the snapped long edge.
The host keeps a least-recently-used tick per entry and frees the textures when the list changes.

A thumbnail job decodes the source file.
For a RAW file the host prefers the embedded thumbnail.[^load]

The UI thread uploads ready thumbnails in the frame loop, at most two per frame.

[^dock]: DockLayout.h at v0.3.12
[^backend]: ImGuiBackend.cpp at v0.3.12
[^filmstrip]: Filmstrip.h at v0.3.12
[^load]: ImageLoad.cpp at v0.3.12
[^docs]: workspace-format.md at v0.3.12
[^appstate]: AppState.h at v0.3.12
[^cmake]: CMakeLists.txt at v0.3.12
