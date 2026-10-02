---
type: Reference
title: Tested third-party plugins
description: Third-party OFX plugins that work in the host.
tags: [ofx, plugins, third-party]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: main
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/main.cpp
    title: main.cpp at v0.3.12
---

# Tested plugins

| Plugin | Effect |
| ------ | ------ |
| [spektrafilm-ofx](https://github.com/chaert-s/spektrafilm-ofx) | Film simulation: film, print, scan, grain, halation, and diffusion |
| [ntsc-rs](https://github.com/valadaptive/ntsc-rs) | Analog video and VHS-style effects |
| [purzOS](https://github.com/purzbeats/purzos-ofx) | 64 native OpenFX plugins with retro and analog looks, glitch, datamosh, CRT and VHS artifacts, color grades, and optical warps |

The host loads any plugin that declares the filter context.[^plugins]
A confirmation of another plugin is welcome as a README pull request.[^readme]

# Metal path

The spektrafilm diffuse and flow effects use the Metal render path since v0.3.10.[^readme]
v0.3.11 fixed a readback problem that produced blank spektrafilm output.
See [Metal render](/architecture/metal-render.md).

# Self-test exclusion

The self-test skips the plugin with identifier `org.spektrafilm.dev`.[^main]
The render case for that plugin is therefore not part of a self-test pass.

[^readme]: README.md at v0.3.12
[^plugins]: OfxPlugins.cpp at v0.3.12
[^main]: main.cpp at v0.3.12
