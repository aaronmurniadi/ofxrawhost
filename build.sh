#!/bin/sh
# Builds OfxRawHost.app (universal arm64 + x86_64). Self-check: OfxRawHost.app/Contents/MacOS/OfxRawHost --selftest
set -e
cd "$(dirname "$0")"
APP=OfxRawHost.app
mkdir -p "$APP/Contents/MacOS"
clang++ -std=c++17 -O2 -fobjc-arc -Wall -mmacosx-version-min=12.0 -arch arm64 -arch x86_64 \
  -Ithird_party/openfx/include OfxRawHost.mm \
  -framework Cocoa -framework CoreImage -framework UniformTypeIdentifiers \
  -o "$APP/Contents/MacOS/OfxRawHost"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>OfxRawHost</string>
  <key>CFBundleIdentifier</key><string>com.aaronmurniadi.ofxrawhost</string>
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
codesign --force --sign - "$APP"
echo "Built $(pwd)/$APP"
