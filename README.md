<img src="docs/logo.png" width="128" alt="OFX Raw Host logo">

# OFX Raw Host

[![Latest release](https://img.shields.io/github/v/release/aaronmurniadi/ofxrawhost)](https://github.com/aaronmurniadi/ofxrawhost/releases/latest)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey)
![Built with](https://img.shields.io/badge/C%2B%2B17%20%2B%20Dear%20ImGui-lightgrey)

<a href="https://www.buymeacoffee.com/aaronmurniadi"><img src="https://www.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=aaronmurniadi&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff" alt="Buy me a coffee"></a>

A minimal still-image [OpenFX](https://github.com/AcademySoftwareFoundation/openfx) host. Open a RAW (or PNG/JPEG/TIFF/EXR) photo, run it through an OFX filter plugin, preview, and export to TIFF, PNG, JPEG or OpenEXR. No video editor needed.

It was written with [spektrafilm-ofx](https://github.com/chaert-s/spektrafilm-ofx) in mind — a film-simulation plugin (film, print, scan, grain, halation, diffusion) that normally only runs inside DaVinci Resolve or Nuke. With this you can use it on photos directly.

> **v0.2** rewrites the app from Objective-C++/AppKit to **C++17 + Dear ImGui** (GLFW), with LibRaw / lcms2 / stb / tinyexr for I/O, so the same codebase can target macOS, Linux, and Windows. Packaged installers are still macOS-only for now.

![OFX Raw Host](docs/screenshot.png)

---

## Install (macOS)

Download the DMG for your Mac from [Releases](https://github.com/aaronmurniadi/ofxrawhost/releases/latest):

- **Apple Silicon (M1 and later):** `OfxRawHost-macOS-arm64.dmg`
- **Intel:** `OfxRawHost-macOS-x86_64.dmg`

Open it and drag **OfxRawHost** onto **Applications**. The app is ad-hoc signed, so on first launch right-click it and choose **Open**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/OfxRawHost.app
```

---

## Installing spektrafilm-ofx

OFX Raw Host loads plugins from the platform default OFX directory plus any directories in `OFX_PLUGIN_PATH`:

| Platform | Default path |
| --- | --- |
| macOS | `/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins` |
| Windows | `C:\Program Files\Common Files\OFX\Plugins` |

The easiest way on macOS is the installer from [spektrafilm.114c.de](https://spektrafilm.114c.de). It puts the plugins in `/Library/OFX/Plugins`, where OFX Raw Host (and Resolve) find them on their own.

To build from source, follow the steps in the [spektrafilm-ofx README](https://github.com/chaert-s/spektrafilm-ofx#setup-from-a-fresh-checkout) (`./build_macos.sh`), then copy the resulting bundles into place:

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

Requires CMake 3.16+, a C++17 compiler, [LibRaw](https://www.libraw.org/), and [Little CMS 2](https://www.littlecms.com/). GLFW and Dear ImGui are fetched automatically by CMake.

```sh
# macOS
brew install cmake libraw little-cms2

# Debian/Ubuntu
sudo apt install cmake pkg-config libraw-dev liblcms2-dev libgl1-mesa-dev xorg-dev

git clone --recursive https://github.com/aaronmurniadi/ofxrawhost.git
cd ofxrawhost
./build.sh
```

On macOS this produces `build/OfxRawHost.app`. Elsewhere, `build/OfxRawHost`.

```sh
# macOS self-test
build/OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest

# Linux / Windows
build/OfxRawHost --selftest
```

---

## Roadmap

| Feature | Status |
| --- | --- |
| C++17 + Dear ImGui UI (cross-platform codebase) | Done (v0.2) |
| Support for the [ntsc-rs](https://github.com/valadaptive/ntsc-rs) OFX plugin (analog video/VHS effects) | Planned |
| Chaining multiple OFX plugins, with reordering | Planned |
| Packaged Windows and Linux releases | Planned |

---

## License

See [LICENSE](LICENSE). The OpenFX SDK in `third_party/openfx` (git submodule) is under its own license. Vendored headers under `third_party/stb`, `third_party/tinyexr`, and `third_party/portable-file-dialogs` keep their upstream licenses.
