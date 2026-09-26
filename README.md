<p align="center">
  <img src="docs/logo.png" width="128" alt="OFX Raw Host logo">
</p>

# OFX Raw Host

[![Latest release](https://img.shields.io/github/v/release/aaronmurniadi/ofxrawhost)](https://github.com/aaronmurniadi/ofxrawhost/releases/latest)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey)
![Built with](https://img.shields.io/badge/C%2B%2B17%20%2B%20Dear%20ImGui-lightgrey)

<a href="https://www.buymeacoffee.com/aaronmurniadi"><img src="https://img.buymeacoffee.com/button-api/?text=Buy%20me%20a%20coffee&emoji=&slug=aaronmurniadi&button_colour=FFDD00&font_colour=000000&font_family=Cookie&outline_colour=000000&coffee_colour=ffffff" alt="Buy me a coffee"></a>

A minimal still-image [OpenFX](https://github.com/AcademySoftwareFoundation/openfx) **plugin host**. Open a RAW (or PNG/JPEG/TIFF/EXR) photo, run it through one or more OFX filter plugins, preview the result, and export to PNG or JPEG — no video NLE required.

Use it to try OFX effects that normally only run inside Resolve, Nuke, or similar hosts, on still photos and a simple processing chain.

> **v0.3.4** adds selectable ImGui themes (Photoshop default) from [ImThemes](https://github.com/Patitotective/ImThemes). **v0.3.3** added libtiff TIFF open (incl. half float) and dropped TIFF/EXR export. **v0.3.2** added preview zoom/pan and a searchable add-plugin list. Packaged installers are still macOS-only for now.

![OFX Raw Host](docs/screenshot.png)

## Demo

![OFX Raw Host demo](docs/demo.gif)

---

## Tested OFX plugins

| Plugin              | What it does                                                    | Link                                                                                                                         |
| ------------------- | --------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| **spektrafilm-ofx** | Film simulation (film, print, scan, grain, halation, diffusion) | [chaert-s/spektrafilm-ofx](https://github.com/chaert-s/spektrafilm-ofx) · [spektrafilm.114c.de](https://spektrafilm.114c.de) |
| **ntsc-rs**         | Analog video / VHS-style effects                                | [valadaptive/ntsc-rs](https://github.com/valadaptive/ntsc-rs)                                                                |

Other OFX filter plugins may work. If you confirm one, a PR to this table is welcome.

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

## Installing OFX plugins

OFX Raw Host loads plugins from the platform default OFX directory plus any paths in `OFX_PLUGIN_PATH`:

| Platform | Default path                                |
| -------- | ------------------------------------------- |
| macOS    | `/Library/OFX/Plugins`                      |
| Linux    | `/usr/OFX/Plugins`                          |
| Windows  | `C:\Program Files\Common Files\OFX\Plugins` |

Install plugins the way their authors recommend (installer, package, or copy `*.ofx.bundle` into the directory above). For example on macOS:

```sh
sudo mkdir -p /Library/OFX/Plugins
sudo cp -R path/to/*.ofx.bundle /Library/OFX/Plugins/
```

Or point the host at a build directory without copying (apps launched from Finder don't see shell variables, so start it from the terminal):

```sh
OFX_PLUGIN_PATH=/path/to/plugins /Applications/OfxRawHost.app/Contents/MacOS/OfxRawHost
```

Restart OFX Raw Host after installing plugins. If none are found, the status bar says so.

---

## Build

Requires CMake 3.16+, a C++17 compiler, [LibRaw](https://www.libraw.org/), [libtiff](https://libtiff.gitlab.io/libtiff/), and [Little CMS 2](https://www.littlecms.com/). GLFW and Dear ImGui are fetched automatically by CMake.

```sh
# macOS
brew install cmake libraw libtiff little-cms2

# Debian/Ubuntu
sudo apt install cmake pkg-config libraw-dev libtiff-dev liblcms2-dev libgl1-mesa-dev xorg-dev

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

| Feature                                         | Status        |
| ----------------------------------------------- | ------------- |
| C++17 + Dear ImGui UI (cross-platform codebase) | Done (v0.2)   |
| Plugin chaining with reorder / bypass           | Done (v0.3)   |
| Preview zoom / pan                              | Done (v0.3.2) |
| TIFF open via libtiff (incl. half float)        | Done (v0.3.3) |
| Selectable ImGui themes (Photoshop default)     | Done (v0.3.4) |
| Packaged Windows and Linux releases             | Planned       |

---

## License

See [LICENSE](LICENSE). The OpenFX SDK in `third_party/openfx` (git submodule) is under its own license. Vendored headers under `third_party/stb`, `third_party/tinyexr`, `third_party/portable-file-dialogs`, and `third_party/fontawesome` keep their upstream licenses. UI themes in `src/Themes.cpp` are from [ImThemes](https://github.com/Patitotective/ImThemes) (MIT).
