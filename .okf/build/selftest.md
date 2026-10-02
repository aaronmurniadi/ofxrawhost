---
type: Attested Computation
title: Run the self-test
description: Sanctioned self-test of the built binary with --selftest.
tags: [build, computation, verification]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
runtime: shell
parameters:
  - { name: binary, type: string, required: true }
executor:
  resource: /build/selftest.md
  receipt: [exit_code, stdout_lines]
attester:
  resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/main.cpp
sources:
  - id: main
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/src/main.cpp
    title: main.cpp at v0.3.12
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
---

# Computation

```sh
<binary> --selftest
```

`<binary>` is `build/OfxRawHost.app/Contents/MacOS/OfxRawHost` on macOS and `build/OfxRawHost` elsewhere.[^readme]
The self-test needs no window and no GL context.[^main]

# Checks

| Area | Check |
| ---- | ----- |
| TIFF decode | A 16-bit integer file and a half-float file load with the expected pixels |
| Color detection | An untagged half-float TIFF maps to Linear Rec.2020; an untagged integer TIFF and an untagged PNG map to sRGB |
| Row order | Source buffers are bottom-up |
| Crop plugin | Defaults give an identity render and a full-size region of definition |
| Crop zoom | The crop slider shrinks the region of definition and changes the output |
| Crop pan | The offsets produce black fill outside the source |
| Plugin set | Every installed filter plugin renders finite, non-empty output |
| Export | PNG and JPEG writes succeed |
| Concurrency | Worker renders, graph mutation, output tag writes, and one export run together for 200 iterations |

# Behavior

A passing case prints `ok  <label>`.[^main]
A skipped case prints `skip  <label>`.
A failure prints `selftest FAILED: <reason>` and exits with code 1.

The run fails when the host finds no filter plugin at all.

The identifier `org.spektrafilm.dev` is skipped in the render loop and in the concurrency case.[^main]
See [Tested third-party plugins](/plugins/third-party-plugins.md).

# Receipt

The executor records the exit code and the raw standard output lines.

# Attester

The self-test is its own deterministic attester, the function `selfTest` in `src/main.cpp`.[^main]
The receipt passes when the exit code is 0 and the output holds one `ok` line per usable plugin, one `ok  Crop zoom` line, and one `ok  concurrency` line.

# Related

See [Threading model](/architecture/threading-model.md) for the concurrency case.
See [Build OFX Raw Host](/build/build-app.md) for the binary.

[^main]: main.cpp at v0.3.12
[^readme]: README.md at v0.3.12
