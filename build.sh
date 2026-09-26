#!/bin/sh
# Builds build/OfxRawHost.app (macOS) or build/OfxRawHost (elsewhere).
# ARCHS=arm64 or ARCHS=x86_64 selects a single macOS architecture (default: host).
# Self-check: build/OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest
set -e
cd "$(dirname "$0")"
BUILD=build
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"
if [ -n "${ARCHS:-}" ] && [ "$(uname)" = Darwin ]; then
  CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_OSX_ARCHITECTURES=$ARCHS"
fi
if [ "${OFX_NATIVE_ARCH:-0}" = "1" ]; then
  CMAKE_ARGS="$CMAKE_ARGS -DOFX_NATIVE_ARCH=ON"
fi
cmake -S . -B "$BUILD" $CMAKE_ARGS
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

if [ "$(uname)" = Darwin ]; then
  APP="$BUILD/OfxRawHost.app"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  BIN="$BUILD/OfxRawHost"
  [ -x "$BIN" ] || BIN="$BUILD/Release/OfxRawHost"
  cp "$BIN" "$APP/Contents/MacOS/OfxRawHost"
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
  codesign --force --sign - "$APP" 2>/dev/null || true
  echo "Built $(pwd)/$APP"
else
  echo "Built $(pwd)/$BUILD/OfxRawHost"
fi
