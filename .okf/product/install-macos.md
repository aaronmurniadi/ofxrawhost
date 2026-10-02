---
type: Installation Guide
title: Install OFX Raw Host on macOS
description: DMG install steps and the ad-hoc signing workaround.
tags: [install, macos, release]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
  - id: build-script
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/build.sh
    title: build.sh at v0.3.12
---

# Install

1. Download the DMG for your Mac from [Releases](/build/release-process.md).
   - Apple Silicon: `OfxRawHost-macOS-arm64.dmg`
   - Intel: `OfxRawHost-macOS-x86_64.dmg`
2. Open the DMG.
3. Drag `OfxRawHost` onto `Applications`.

# First launch

The app is ad-hoc signed only.[^readme]
The system therefore blocks the first launch.

Do one of these two steps:

- Right-click the app and choose **Open**.
- Run this command:

```sh
xattr -dr com.apple.quarantine /Applications/OfxRawHost.app
```

The app declares a minimum system version of 12.0.[^build-script]

# Next step

Install OFX plugins.
See [Plugin discovery](/product/plugin-discovery.md).

[^readme]: README.md at v0.3.12
[^build-script]: build.sh at v0.3.12
