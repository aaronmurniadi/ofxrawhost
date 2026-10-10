#!/bin/sh
# Builds <build>/AppImage/OfxRawHost-Linux-<arch>.AppImage from an already built
# binary. Needs APPIMAGETOOL (path or name of appimagetool) and, when the binary
# links shared libraries from the system, ldd.
#
# Usage: VERSION=1.2.3 APPIMAGETOOL=/path/to/appimagetool packaging/linux/build-appimage.sh
set -e
cd "$(dirname "$0")/../.."

BUILD=${BUILD_DIR:-build}
OUT=${OUT_DIR:-$BUILD/AppImage}
NAME=OfxRawHost
APPDIR="$OUT/$NAME.AppDir"
TOOL=${APPIMAGETOOL:-appimagetool}
VERSION=${VERSION:-0.0.0}

case "$(uname -m)" in
  aarch64|arm64) APPTARGET=arm_aarch64 ;;
  *) APPTARGET=x86_64 ;;
esac

BIN="$BUILD/$NAME"
if [ ! -x "$BIN" ]; then
  BIN="$BUILD/Release/$NAME"
fi
if [ ! -x "$BIN" ]; then
  echo "error: no built binary at $BUILD/$NAME; build with cmake first" >&2
  exit 1
fi
if ! command -v "$TOOL" >/dev/null 2>&1 && [ ! -x "$TOOL" ]; then
  echo "error: appimagetool not found; set APPIMAGETOOL to its path" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/metainfo"

cp "$BIN" "$APPDIR/usr/bin/$NAME"
# The host reads the icon font next to its own binary.
cp "$BUILD/fa-solid-900.ttf" "$APPDIR/usr/bin/"
# Bundled OFX plugins are found in <exe dir>/Plugins.
if [ -d "$BUILD/Plugins/OfxRawHost.ofx.bundle" ]; then
  mkdir -p "$APPDIR/usr/bin/Plugins"
  cp -R "$BUILD/Plugins/OfxRawHost.ofx.bundle" "$APPDIR/usr/bin/Plugins/"
fi
cp packaging/linux/ofxrawhost.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/ofxrawhost.png"
cp packaging/linux/ofxrawhost.png "$APPDIR/ofxrawhost.png"
cp packaging/linux/ofxrawhost.desktop "$APPDIR/usr/share/applications/ofxrawhost.desktop"
cp packaging/linux/ofxrawhost.desktop "$APPDIR/ofxrawhost.desktop"
cp packaging/linux/ofxrawhost.appdata.xml "$APPDIR/usr/share/metainfo/"
cp packaging/linux/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
for doc in README.md LICENSE; do
  if [ -f "$doc" ]; then cp "$doc" "$APPDIR/"; fi
done

# Library exclusions follow the AppImage excludelist: these must come from the
# host system, because substituting them breaks the runtime or the GPU driver.
isSystemLib() {
  case "$1" in
    ld-linux*) return 0 ;;
    libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libresolv.so*|libnsl.so*) return 0 ;;
    libgcc_s.so*|libstdc++.so*|libz.so*|libexpat.so*) return 0 ;;
    libGL.so*|libGLX*|libEGL*|libOpenGL*|libGLdispatch*) return 0 ;;
    libdrm.so*|libglib-2.0*|libgobject-2.0*|libgio-2.0*|libgmodule-2.0*|libgthread-2.0*) return 0 ;;
    libX11.so*|libX11-xcb*|libxcb.so*|libXau.so*|libXdmcp.so*|libXext.so*) return 0 ;;
    libXrender.so*|libXfixes.so*|libXrandr.so*|libXi.so*|libXcursor.so*|libXinerama.so*) return 0 ;;
    libXxf86vm.so*|libxshmfence.so*|libfontconfig.so*|libfreetype.so*|libwayland-*) return 0 ;;
  esac
  return 1
}

if command -v ldd >/dev/null 2>&1; then
  ldd "$APPDIR/usr/bin/$NAME" | awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i }' |
    sort -u | while read -r lib; do
      name=$(basename "$lib")
      if isSystemLib "$name"; then continue; fi
      cp -L "$lib" "$APPDIR/usr/lib/$name"
    done
fi

ARCH="$APPTARGET" VERSION="$VERSION" NO_STRIP=1 APPIMAGE_EXTRACT_AND_RUN=1 \
  "$TOOL" "$APPDIR" "$OUT/$NAME-Linux-$APPTARGET.AppImage"
echo "Built $(pwd)/$OUT/$NAME-Linux-$APPTARGET.AppImage"
