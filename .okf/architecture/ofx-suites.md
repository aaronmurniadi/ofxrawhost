---
type: Reference
title: OFX suites
description: The six OFX suites the host serves and the functions in each.
tags: [ofx, architecture]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: suites
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxSuites.cpp
    title: OfxSuites.cpp at v0.3.12
  - id: host
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/ofx/OfxHost.h
    title: OfxHost.h at v0.3.12
---

# Served suites

`fetchSuite` returns one of six suites by name:[^suites]

| Suite | Purpose |
| ----- | ------- |
| `kOfxPropertySuite` | Property get and set for every OFX handle |
| `kOfxParameterSuite` | Parameter definition and values |
| `kOfxImageEffectSuite` | Instance, clips, images, and regions of definition |
| `kOfxMessageSuite` | Plugin messages to the host |
| `kOfxMemorySuite` | Image memory allocation |
| `kOfxMultiThreadSuite` | Parallel slice execution, mutexes, and CPU count |

The host returns no suite for progress, timeline, interact, or draw requests.

# Image effect suite

The image effect suite implements these calls:[^suites]

- `getPropertySet` and `getParamSet`
- `clipDefine`, `clipGetHandle`, and `clipGetPropertySet`
- `clipGetImage` and `clipReleaseImage`
- `clipGetRegionOfDefinition`
- `imageMemoryAlloc`, `imageMemoryFree`, `imageMemoryLock`, and `imageMemoryUnlock`

`clipGetImage` reuses one image property set per clip.
The host therefore avoids an allocation per image request.

# Parameter suite

The parameter suite supports definition, handle lookup, value get and set, value at time, copy, and edit begin and end.[^suites]
Keyframe calls return `kOfxStatErrUnsupported` or a benign success, because the host has no animation.

Parameter dimensions follow the parameter type: a scalar has one value, a 2D type two, RGB three, and RGBA four.[^suites]

# Multi-thread suite

The host serves a worker pool of `hardware_concurrency` minus one workers, plus the calling thread.[^suites]
A worker takes the next slice index from an atomic counter, so the slices stay balanced.
One job runs at a time.
A nested request from a pool thread falls back to ephemeral threads.

# Message suite

A plugin message forwards to the host callback `gOnMessage`.[^host]
The callback runs on the thread that raised the message.

# Memory suite

The host allocates and frees image memory on request.[^suites]
Lock and unlock succeed without extra work, because the memory is already resident.

[^suites]: OfxSuites.cpp at v0.3.12
[^host]: OfxHost.h at v0.3.12
