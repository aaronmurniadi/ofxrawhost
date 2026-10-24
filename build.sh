#!/bin/sh
# Builds build/OfxRawHost.app (macOS) or build/OfxRawHost (elsewhere).
# ARCHS=arm64 or ARCHS=x86_64 selects a single macOS architecture (default: host).
# Self-check: TEST=1 ./build.sh, or build/OfxRawHostTests, or the app with --selftest
set -e
cd "$(dirname "$0")"
BUILD=build
# The style rule that a script can check, before the build that would show it
# much later. Skipped when python3 is unavailable, so a build never depends on it.
if command -v python3 >/dev/null 2>&1; then
  python3 scripts/check-ternaries.py
fi
# The release workflow passes VERSION_LABEL (the git tag) and VERSION. A local
# build takes the nearest git tag for both.
if [ -z "${VERSION_LABEL:-}" ]; then
  VERSION_LABEL=$(git describe --tags --abbrev=0 2>/dev/null)
fi
VERSION_LABEL="${VERSION_LABEL:-v0.0.0}"
if [ -z "${VERSION:-}" ]; then
  VERSION=$(printf '%s' "$VERSION_LABEL" | sed -n 's/^v\([0-9][0-9.]*\).*/\1/p')
fi
VERSION="${VERSION:-0.0.0}"
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release -DOFX_VERSION_LABEL=$VERSION_LABEL"
if [ -n "${ARCHS:-}" ] && [ "$(uname)" = Darwin ]; then
  CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES=$ARCHS"
fi
if [ "${OFX_NATIVE_ARCH:-0}" = "1" ]; then
  CMAKE_ARGS="$CMAKE_ARGS -DOFX_NATIVE_ARCH=ON"
fi
cmake -S . -B "$BUILD" $CMAKE_ARGS
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

# TEST=1 also runs the self-test, which needs the plugins to be built.
if [ "${TEST:-0}" = "1" ]; then
  TESTS="$BUILD/OfxRawHostTests"
  [ -x "$TESTS" ] || TESTS="$BUILD/Release/OfxRawHostTests"
  "$TESTS"
fi

if [ "$(uname)" = Darwin ]; then
  APP="$BUILD/OfxRawHost.app"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/PlugIns"
  BIN="$BUILD/OfxRawHost"
  [ -x "$BIN" ] || BIN="$BUILD/Release/OfxRawHost"
  cp "$BIN" "$APP/Contents/MacOS/OfxRawHost"
  # --selftest forwards to this binary, so it travels inside the bundle.
  TESTS="$BUILD/OfxRawHostTests"
  [ -x "$TESTS" ] || TESTS="$BUILD/Release/OfxRawHostTests"
  if [ -x "$TESTS" ]; then
    cp "$TESTS" "$APP/Contents/MacOS/OfxRawHostTests"
  fi
  if [ -f docs/logo.png ]; then
    ICONSET=$(mktemp -d)/AppIcon.iconset
    mkdir "$ICONSET"
    for s in 16 32 128 256 512; do
      sips -z $s $s docs/logo.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
      sips -z $((s*2)) $((s*2)) docs/logo.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
  fi
  cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>OfxRawHost</string>
  <key>CFBundleIdentifier</key><string>com.aaronmurniadi.ofxrawhost</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundleName</key><string>OFX Raw Host</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION:-0.0.0}</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>CFBundleDocumentTypes</key><array><dict>
    <key>CFBundleTypeRole</key><string>Viewer</string>
    <key>LSItemContentTypes</key><array><string>public.image</string></array>
  </dict></array>
</dict></plist>
EOF
  if [ -d "$BUILD/Plugins/OfxRawHost.ofx.bundle" ]; then
    rm -rf "$APP/Contents/PlugIns/OfxRawHost.ofx.bundle"
    cp -R "$BUILD/Plugins/OfxRawHost.ofx.bundle" "$APP/Contents/PlugIns/"
  fi
  codesign --force --sign - "$APP" 2>/dev/null || true
  echo "Built $(pwd)/$APP"
else
  echo "Built $(pwd)/$BUILD/OfxRawHost"
fi
