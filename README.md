<img src="docs/logo.png" width="128" alt="OFX Raw Host logo">

# OFX Raw Host

[![Latest release](https://img.shields.io/github/v/release/aaronmurniadi/ofxrawhost)](https://github.com/aaronmurniadi/ofxrawhost/releases/latest)
![Platform: macOS](https://img.shields.io/badge/platform-macOS%2012%2B-lightgrey)

<a href="https://www.buymeacoffee.com/aaronmurniadi"><img src="https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=aaronmurniadi&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff" alt="Buy me a coffee"></a>

> [!NOTE]
> OFX Raw Host is currently **macOS only**. Windows and Linux versions are planned. If you'd like to see them sooner, [buy me a coffee](https://www.buymeacoffee.com/aaronmurniadi) to support that work.

A minimal still-image [OpenFX](https://github.com/AcademySoftwareFoundation/openfx) host for macOS. Open a RAW (or any ImageIO-readable) photo, run it through an OFX filter plugin, preview, and export to TIFF, PNG, JPEG or OpenEXR — no video editor required.

The main candidate plugin is [spektrafilm-ofx](https://github.com/chaert-s/spektrafilm-ofx), a film-simulation plugin (film, print, scan, grain, halation, diffusion) normally used inside DaVinci Resolve or Nuke. OFX Raw Host lets you use it on photos directly.

![OFX Raw Host](docs/screenshot.png)

---

## Install

Download the DMG for your Mac from [Releases](https://github.com/aaronmurniadi/ofxrawhost/releases/latest):

- **Apple Silicon (M1 and later):** `OfxRawHost-macOS-arm64.dmg`
- **Intel:** `OfxRawHost-macOS-x86_64.dmg`

Open it and drag **OfxRawHost** onto **Applications**. The app is ad-hoc signed, so on first launch right-click it and choose **Open**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/OfxRawHost.app
```

---

## Installing spektrafilm-ofx

OFX Raw Host loads plugins from `/Library/OFX/Plugins` (the standard macOS OFX location) plus any directories in `OFX_PLUGIN_PATH`.

**Prebuilt installer (easiest):** download the macOS installer from [spektrafilm.114c.de](https://spektrafilm.114c.de) and run it. It installs the plugins into `/Library/OFX/Plugins`, so they show up in OFX Raw Host (and Resolve) automatically.

**From source:** follow the build steps in the [spektrafilm-ofx README](https://github.com/chaert-s/spektrafilm-ofx#setup-from-a-fresh-checkout) (`./build_macos.sh`), then copy the resulting bundles into place:

```sh
sudo mkdir -p /Library/OFX/Plugins
sudo cp -R path/to/*.ofx.bundle /Library/OFX/Plugins/
```

Or skip the copy and point the host at your build directory (apps launched from Finder don't see shell variables, so start it from the terminal):

```sh
OFX_PLUGIN_PATH=/path/to/spektrafilm-ofx/build /Applications/OfxRawHost.app/Contents/MacOS/OfxRawHost
```

Restart OFX Raw Host after installing plugins. If none are found, the status bar says so.

---

## Build

Requires Xcode Command Line Tools and macOS 12+.

```sh
git clone --recursive https://github.com/aaronmurniadi/ofxrawhost.git
cd ofxrawhost
./build.sh
build/OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest
```

---

## License

See [LICENSE](LICENSE). The OpenFX SDK in `third_party/openfx` (git submodule) is under its own license.
