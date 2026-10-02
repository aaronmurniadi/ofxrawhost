---
type: Reference
title: Build dependencies
description: Required tools, libraries, and third-party sources.
tags: [build, dependencies]
status: draft
generated: { by: pi/deepseek-v4.1-flash, at: 2026-10-02T15:08:56Z }
sources:
  - id: cmake
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/CMakeLists.txt
    title: CMakeLists.txt at v0.3.12
  - id: readme
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/README.md
    title: README.md at v0.3.12
  - id: mise
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/mise.toml
    title: mise.toml at v0.3.12
  - id: gitmodules
    resource: https://github.com/aaronmurniadi/ofxrawhost/blob/v0.3.12/.gitmodules
    title: .gitmodules at v0.3.12
---

# Required tools

| Tool | Minimum |
| ---- | ------- |
| CMake | 3.16 |
| C++ compiler | C++17 |

`mise.toml` pins CMake 4.4.0 and Ninja 1.13.2 for the development environment.[^mise]

# Required libraries

| Library | Purpose |
| ------- | ------- |
| LibRaw | RAW decode |
| libtiff | TIFF decode |
| Little CMS 2 | ICC profile handling and color conversion |
| zlib | PNG compression for the ICC chunk path |
| OpenGL | Texture upload and the ImGui backend |

Install the libraries with one command.[^readme]

```sh
# macOS
brew install cmake libraw libtiff little-cms2

# Debian and Ubuntu
sudo apt install cmake pkg-config libraw-dev libtiff-dev liblcms2-dev libgl1-mesa-dev xorg-dev
```

The configure step fails with a clear message when LibRaw, libtiff, or Little CMS 2 is missing.[^cmake]

# Fetched and vendored sources

GLFW 3.4 and Dear ImGui `v1.91.8-docking` arrive through `FetchContent`.[^cmake]
The repository vendors stb, tinyexr, portable-file-dialogs, and Font Awesome under `third_party/`.
The OpenFX SDK is a git submodule at `third_party/openfx`.[^gitmodules]

Clone the repository with submodules:

```sh
git clone --recursive https://github.com/aaronmurniadi/ofxrawhost.git
```

[^cmake]: CMakeLists.txt at v0.3.12
[^readme]: README.md at v0.3.12
[^mise]: mise.toml at v0.3.12
[^gitmodules]: .gitmodules at v0.3.12
