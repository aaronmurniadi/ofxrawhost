---
type: Attested Computation
title: Build OFX Raw Host
description: Sanctioned build of the app bundle with ./build.sh.
tags: [build, computation]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
runtime: shell
parameters:
  - { name: archs, type: string, required: false }
  - { name: native_arch, type: boolean, required: false }
  - { name: version, type: string, required: false }
executor:
  resource: /build/build-app.md
  receipt: [exit_code, output_path, architecture_line]
attester:
  resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/.github/workflows/release.yml
sources:
  - id: build-script
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/build.sh
    title: build.sh at v0.3.12
  - id: workflow
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/.github/workflows/release.yml
    title: release.yml at v0.3.12
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
---

# Computation

```sh
ARCHS=<archs> OFX_NATIVE_ARCH=<native_arch> VERSION=<version> ./build.sh
```

The executor fills the declared parameters and runs the line from the repository root.
The executor must not edit the script.[^build-script]
An omitted parameter uses the default from the table below.

# Parameters

| Parameter | Default | Effect |
| --------- | ------- | ------ |
| `archs` | host architecture | Sets `CMAKE_OSX_ARCHITECTURES` on macOS |
| `native_arch` | false | Adds `-march=native` or `/arch:AVX2` for the build host CPU |
| `version` | `0.0.0` | Sets `CFBundleShortVersionString` in the app bundle |

The value 1 enables native architecture optimization.[^build-script]
The `archs` parameter applies on macOS only.

# Result

| Platform | Output |
| -------- | ------ |
| macOS | `build/OfxRawHost.app` |
| Linux and Windows | `build/OfxRawHost` |

The macOS branch also does the following:[^build-script]

- Builds the app icon from `docs/logo.png` when the file exists.
- Writes `Contents/Info.plist` with the bundle identifier `com.aaronmurniadi.ofxrawhost` and a minimum system version of 12.0.
- Copies `OfxRawHost.ofx.bundle` into `Contents/PlugIns`.
- Signs the bundle ad-hoc, and continues when signing fails.

# Receipt

The executor records this evidence:

- The exit code of the script.
- The path of the produced app or binary.
- On macOS, the architecture line from `lipo -info`.

# Attester

The release workflow verifies the binary architecture with `lipo -info` and fails when the value does not match the target.[^workflow]
The workflow also fails when the artifact is absent.

# Notes

Link-time optimization is enabled when the toolchain supports it.[^cmake]
See [Build dependencies](/build/dependencies.md), [Run the self-test](/build/selftest.md), and [Release process](/build/release-process.md).

[^build-script]: build.sh at v0.3.12
[^workflow]: release.yml at v0.3.12
[^cmake]: CMakeLists.txt at v0.3.12
