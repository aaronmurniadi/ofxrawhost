---
type: Behavior
title: Metal render
description: GPU render path for Apple systems and the OFX_HOST_METAL switch.
tags: [ofx, metal, render]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: suites
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxSuites.cpp
    title: OfxSuites.cpp at v0.3.12
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: metal
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxMetal.h
    title: OfxMetal.h at v0.3.12
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
---

# Availability

The Metal path exists on Apple systems only.[^suites]
The host reports `kOfxImageEffectPropMetalRenderSupported` as `true` there.
Set `OFX_HOST_METAL=0` to disable Metal at runtime.[^metal]

# Eligibility

A plugin must declare `kOfxImageEffectPropMetalRenderSupported` to use Metal.[^plugins]
The host records the declaration at load time and logs this line:

```text
[metal] plugin '<identifier>' declares Metal render support
```

# Render path

The host does the following for a Metal render:[^plugins]

1. Set `kOfxImageEffectPropMetalEnabled` to 1 and pass the host command queue.
2. Create shared-storage buffers for the source and destination images.
3. Copy the source pixels into the source buffer.
4. Call the render action.
5. Wait for the GPU with `ofxMetalSync`.
6. Copy the destination buffer back into the float output.

Buffer creation happens per render.
The host releases both buffers after the render.

# Failure reporting

The host logs a failure line when a Metal-capable plugin fails a render.[^plugins]
The line carries the status value in decimal and hexadecimal, the Metal flag, and the plugin pointer.

# History

The Metal path for the spektrafilm diffuse and flow effects arrived in v0.3.10.[^readme]
v0.3.11 fixed a readback problem that produced blank output.
See [Tested third-party plugins](/plugins/third-party-plugins.md).

[^suites]: OfxSuites.cpp at v0.3.12
[^plugins]: OfxPlugins.cpp at v0.3.12
[^metal]: OfxMetal.h at v0.3.12
[^readme]: README.md at v0.3.12
