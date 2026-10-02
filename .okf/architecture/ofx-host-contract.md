---
type: Reference
title: OFX host contract
description: Host properties and capabilities that OFX plugins can query.
tags: [ofx, architecture]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: suites
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxSuites.cpp
    title: OfxSuites.cpp at v0.3.12
  - id: plugins
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxPlugins.cpp
    title: OfxPlugins.cpp at v0.3.12
  - id: gpurender
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/third_party/openfx/include/ofxGPURender.h
    title: ofxGPURender.h at v0.3.12
---

# Identity

| Property | Value |
| -------- | ----- |
| `kOfxPropName` | `local.ofxrawhost` |
| `kOfxPropLabel` | OFX Raw Host |
| `kOfxPropAPIVersion` | 1.4 |
| `kOfxPropVersion` | 0.3.12 |
| `kOfxPropVersionLabel` | `0.3.12` |

# Capabilities

| Property | Value |
| -------- | ----- |
| `kOfxImageEffectPropSupportedContexts` | `kOfxImageEffectContextFilter` |
| `kOfxImageEffectPropSupportedComponents` | `kOfxImageComponentRGBA` |
| `kOfxImageEffectPropSupportedPixelDepths` | `kOfxBitDepthFloat` |
| `kOfxImageEffectHostPropNativeOrigin` | bottom left |
| `kOfxImageEffectHostPropIsBackground` | false |
| `kOfxImageEffectPropSupportsOverlays` | false |
| `kOfxImageEffectPropSupportsMultiResolution` | false |
| `kOfxImageEffectPropSupportsTiles` | false |
| `kOfxImageEffectPropTemporalClipAccess` | false |
| `kOfxImageEffectPropSupportsMultipleClipDepths` | false |
| `kOfxImageEffectPropSupportsMultipleClipPARs` | false |
| `kOfxImageEffectPropSetableFrameRate` | false |
| `kOfxImageEffectPropSetableFielding` | false |

The host is not a background render host and supports one input clip only.

# Render backends

| Property | Value |
| -------- | ----- |
| `kOfxImageEffectPropMetalRenderSupported` | `true` on Apple, `false` elsewhere |
| `kOfxImageEffectPropCPURenderSupported` | `true` on Apple; absent elsewhere |
| `kOfxImageEffectPropOpenGLRenderSupported` | `false` |
| `kOfxImageEffectPropCudaRenderSupported` | `false` |
| `kOfxImageEffectPropOpenCLSupported` | `false` |

The host must declare the OpenGL, CUDA, and OpenCL properties even when it does not support those backends.[^suites]
OpenFX support plugins raise a host-inadequate error when the properties are absent.

The OpenFX header sets the default of `kOfxImageEffectPropCPURenderSupported` to `true` for a host and a plug-in.[^gpurender]
An absent property therefore means the same as `true`, and the CPU render path stays available on every platform.
The property arrived in OFX 1.5.1.

See [Metal render](/architecture/metal-render.md).

# Parameter host

The host reports no animation support for string, choice, boolean, custom, and parametric parameters.[^suites]
`kOfxParamHostPropMaxParameters` is -1, which means no fixed limit.
The maximum page count is 0.
Custom interaction is unsupported.

# Clip properties

Every clip advertises float RGBA, opaque premultiplication, no fields, a pixel aspect ratio of 1, a frame rate of 24, and the frame range `{0, 0}`.[^plugins]
The host marks each clip connected, because the chain has one source.

# Version source

The host properties hard-code the version string.[^suites]
A version bump must update `kOfxPropVersion` and `kOfxPropVersionLabel` together.

[^suites]: OfxSuites.cpp at v0.3.12
[^plugins]: OfxPlugins.cpp at v0.3.12
[^gpurender]: ofxGPURender.h at v0.3.12
