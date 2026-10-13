#!/usr/bin/env bash
# Builds TBS_Receiver-<version>-x86_64.AppImage: the GUI, tbs-daemon and the command-line tools with
# Qt, FFmpeg, ZeroMQ and SRT bundled, runnable on most Linux distributions.
#
#   packaging/appimage/build-appimage.sh [build-dir]
#
# Needs: cmake, a C++17 compiler, Qt 6 (qmake6 or qmake on PATH) and the optional libraries.
# linuxdeploy and its Qt plugin are downloaded into <build-dir>/tools unless TOOLS_DIR has them.
set -euo pipefail

SRC=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$SRC/build-appimage}
mkdir -p "$BUILD"
BUILD=$(cd "$BUILD" && pwd) # absolute: the script changes directory below
TOOLS=${TOOLS_DIR:-$BUILD/tools}
mkdir -p "$TOOLS"
TOOLS=$(cd "$TOOLS" && pwd)
APPDIR=$BUILD/AppDir

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DTBS_BUILD_TESTS=OFF -DTBS_BUILD_PYTHON=OFF
cmake --build "$BUILD" -j"$(nproc)"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"
# Development files are not needed in the AppImage
rm -rf "$APPDIR/usr/include" "$APPDIR/usr/lib/cmake" "$APPDIR"/usr/lib/*.a

mkdir -p "$TOOLS"
for tool in linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
            linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage; do
    file=$TOOLS/$(basename "$tool")
    [ -x "$file" ] || { curl -fsSL -o "$file" "https://github.com/$tool"; chmod +x "$file"; }
done

# Containers usually have no FUSE: the tools extract themselves instead
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$TOOLS:$PATH"
if [ -z "${QMAKE:-}" ]; then
    QMAKE=$(command -v qmake6 || command -v qmake || true)
    [ -n "$QMAKE" ] || { [ -x /usr/lib/qt6/bin/qmake ] && QMAKE=/usr/lib/qt6/bin/qmake; }
    export QMAKE
fi
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SRC/CMakeLists.txt" | head -1)
export VERSION LINUXDEPLOY_OUTPUT_VERSION=$VERSION

executables=()
for exe in tbs-receiver tbs-daemon tbs-cli tbs-bbproc tbs-make-test-stream; do
    [ -x "$APPDIR/usr/bin/$exe" ] && executables+=(--executable "$APPDIR/usr/bin/$exe")
done

cd "$BUILD"
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" "${executables[@]}" \
    --desktop-file "$APPDIR/usr/share/applications/tbs-receiver.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/tbs-receiver.png" \
    --plugin qt --output appimage
ls -l "$BUILD"/*.AppImage
