---
type: Procedure
title: Release process
description: Tag-driven macOS release that publishes DMG artifacts.
tags: [build, release, ci]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: workflow
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/.github/workflows/release.yml
    title: release.yml at v0.3.12
  - id: releases
    resource: https://github.com/aaronmurniadi/ofxrawhost/tree/v0.3.12/docs/releases
    title: docs/releases at v0.3.12
---

# Trigger

The release workflow runs on a tag push that matches `v*`, or on a manual dispatch.[^workflow]

# macOS build matrix

| Runner | Architecture |
| ------ | ------------ |
| `macos-14` | arm64 |
| `macos-15-intel` | x86_64 |

Each job does the following:[^workflow]

1. Check out the repository with submodules.
2. Install cmake, libraw, libtiff, and little-cms2 with Homebrew.
3. Remove the `build` directory.
4. Run `ARCHS=<arch> VERSION=<tag without v> ./build.sh`.
5. Verify the binary architecture with `lipo -info`, and fail on a mismatch.
6. Create a compressed DMG with `hdiutil`, with an `Applications` link.
7. Upload the DMG as an artifact.

# Publish

The publish job runs for tag pushes only.[^workflow]
It downloads the artifacts, builds the release notes, and creates or edits the release.

The notes contain these parts:

- The content of `docs/releases/<tag>.md` when the file exists.
- A Downloads section with both DMG names.
- A full changelog link that compares the previous tag with this tag.

The job uploads both DMGs with `gh release upload --clobber`.

# Notes files

As of v0.3.12, `docs/releases/` holds notes for v0.3.2 through v0.3.9.[^releases]
A tag without a notes file produces a release with the Downloads section and the changelog link only.

# Related

See [Build OFX Raw Host](/build/build-app.md) and [Install OFX Raw Host on macOS](/product/install-macos.md).

[^workflow]: release.yml at v0.3.12
[^releases]: docs/releases at v0.3.12
