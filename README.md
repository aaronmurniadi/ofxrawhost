<img src="docs/logo.png" width="128" alt="OFX Raw Host logo">

# OFX Raw Host

A minimal still-image [OpenFX](https://github.com/AcademySoftwareFoundation/openfx) host for macOS. Open a RAW (or any ImageIO-readable) photo, run it through an OFX filter plugin, preview, and export to TIFF, PNG, JPEG or OpenEXR — no video editor required.

The main candidate plugin is [spektrafilm-ofx](https://github.com/chaert-s/spektrafilm-ofx), a film-simulation plugin (film, print, scan, grain, halation, diffusion) normally used inside DaVinci Resolve or Nuke. OFX Raw Host lets you use it on photos directly.

![OFX Raw Host](docs/screenshot.png)

## Install

Download `OfxRawHost-macOS.zip` from [Releases](https://github.com/aaronmurniadi/ofxrawhost/releases), unzip, and move `OfxRawHost.app` to `/Applications`. The app is ad-hoc signed, so on first launch right-click it and choose **Open**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/OfxRawHost.app
```

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

## Build

Requires Xcode Command Line Tools and macOS 12+.

```sh
git clone --recursive https://github.com/aaronmurniadi/ofxrawhost.git
cd ofxrawhost
./build.sh
OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest
```

## License

See [LICENSE](LICENSE). The OpenFX SDK in `third_party/openfx` (git submodule) is under its own license.
