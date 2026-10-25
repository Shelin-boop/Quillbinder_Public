#!/bin/bash
# Builds the AppImage on Ubuntu 24.04 LTS.
#
#   sudo apt install build-essential cmake qt6-base-dev qt6-base-private-dev qt6-svg-dev qt6-pdf-dev \
#        qt6-wayland libqt6svg6 qt6-xdgdesktopportal-platformtheme qt6-image-formats-plugins \
#        libhunspell-dev zlib1g-dev file
#   (or tools/cloud-setup.sh; a plugin that is not installed is left out of the AppImage with a note)
#   packaging/build-appimage.sh
#
# Result: build-appimage/<Name>-<version>-x86_64.AppImage
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOOLS=${TOOLS:-$ROOT/packaging/tools}
BUILD=${BUILD:-$ROOT/build-appimage}
APPDIR=$BUILD/AppDir
APP_ID=$(sed -n 's/^set(APP_ID *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
APP_NAME=$(sed -n 's/^set(APP_NAME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
VERSION=$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
# "alpha", "beta" or empty: goes into the file name only (Quillbinder-0.30.0-alpha-x86_64.AppImage)
LABEL=$(sed -n 's/^set(APP_VERSION_LABEL *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
QTPLUGINS=${QTPLUGINS:-/usr/lib/x86_64-linux-gnu/qt6/plugins}
export APPIMAGE_EXTRACT_AND_RUN=1   # the tools also run without FUSE (containers)

# ---------------------------------------------------------------- tools
mkdir -p "$TOOLS"
fetch() {
    [ -s "$TOOLS/$1" ] || curl -fsSL -o "$TOOLS/$1" "$2"
    chmod +x "$TOOLS/$1"
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool-x86_64.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
# static runtime: the AppImage does not need libfuse2 (not installed on 24.04 by default)
fetch runtime-x86_64 \
    https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64

# ---------------------------------------------------------------- build
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DQB_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" -j"$(nproc)"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"
strip "$APPDIR/usr/bin/$APP_ID" "$APPDIR"/usr/lib/"$APP_ID"/plugins/*.so 2>/dev/null || true

# ---------------------------------------------------------------- Qt and libraries
export PATH="$TOOLS:$PATH"
export QMAKE=${QMAKE:-/usr/lib/qt6/bin/qmake}
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export EXTRA_QT_MODULES="svg;waylandclient"
export DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1

"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$APP_ID" \
    --desktop-file "$ROOT/packaging/$APP_ID.desktop" \
    --icon-file "$ROOT/resources/app/$APP_ID.svg" \
    --plugin qt

# libraries needed by the bundled exporter plugins
for f in "$APPDIR"/usr/lib/"$APP_ID"/plugins/*.so; do
    [ -f "$f" ] && "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --deploy-deps-only "$f" >/dev/null
done

# Plugins linuxdeploy-plugin-qt does not pick up by itself: desktop portal
# (KDE file dialogs), SVG icons and images, Wayland integration.
copy_plugin() {  # <plugin dir> <file>
    local src="$QTPLUGINS/$1/$2"
    [ -f "$src" ] || { echo "note: $src not installed, skipped"; return; }
    mkdir -p "$APPDIR/usr/plugins/$1"
    if [ ! -f "$APPDIR/usr/plugins/$1/$2" ]; then
        cp "$src" "$APPDIR/usr/plugins/$1/"
        "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --deploy-deps-only "$APPDIR/usr/plugins/$1/$2" >/dev/null
    fi
}
copy_plugin platformthemes libqxdgdesktopportal.so
# invisible platform: used to test a program update before it is installed
copy_plugin platforms libqoffscreen.so
copy_plugin iconengines libqsvgicon.so
# typing accents with dead keys / compose key, and input methods (ibus)
for f in "$QTPLUGINS"/platforminputcontexts/*.so; do copy_plugin platforminputcontexts "$(basename "$f")"; done
for f in libqsvg.so libqjpeg.so libqgif.so libqico.so libqwebp.so; do copy_plugin imageformats "$f"; done
for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    if [ -d "$QTPLUGINS/$d" ]; then
        for f in "$QTPLUGINS/$d"/*.so; do copy_plugin "$d" "$(basename "$f")"; done
    fi
done

# System libraries that every Ubuntu 24.04 desktop has (and that talk to
# system services, so the system's own copy is the better choice anyway).
# Removing them keeps the AppImage small; the program finds them in /usr/lib.
SYSTEM_LIBS=(
    'libglib-2.0.so*' 'libgio-2.0.so*' 'libgobject-2.0.so*' 'libgmodule-2.0.so*' 'libffi.so*'
    'libsystemd.so*' 'libdbus-1.so*' 'libcap.so*' 'libselinux.so*' 'libmount.so*' 'libblkid.so*'
    'libcrypto.so*' 'libssl.so*' 'libgnutls.so*' 'libnettle.so*' 'libhogweed.so*' 'libgmp.so*'
    'libp11-kit.so*' 'libtasn1.so*' 'libidn2.so*' 'libunistring.so*' 'libgcrypt.so*' 'libgpg-error.so*'
    'libkrb5.so*' 'libk5crypto.so*' 'libkrb5support.so*' 'libgssapi_krb5.so*' 'libkeyutils.so*' 'libcom_err.so*'
    'libcurl-gnutls.so*' 'libnghttp2.so*' 'librtmp.so*' 'libssh.so*' 'libpsl.so*' 'libbrotli*.so*'
    'libldap.so*' 'liblber.so*' 'libsasl2.so*' 'libproxy.so*' 'libpxbackend-1.0.so' 'libduktape.so*'
    'libavahi-client.so*' 'libavahi-common.so*' 'libcups.so*'
    'libzstd.so*' 'liblz4.so*' 'liblzma.so*' 'libbz2.so*' 'libz.so*' 'libexpat.so*' 'libpcre2-8.so*'
    'libmd.so*' 'libbsd.so*' 'libXau.so*' 'libXdmcp.so*'
)
for pattern in "${SYSTEM_LIBS[@]}"; do
    rm -f "$APPDIR"/usr/lib/$pattern
done
# Plugins the program does not use (TIFF/MNG/TGA pictures, network) and
# the libraries only they need.
rm -f "$APPDIR"/usr/plugins/imageformats/{libqtiff.so,libqmng.so,libqtga.so,libqwbmp.so,libqicns.so}
rm -rf "$APPDIR"/usr/plugins/tls "$APPDIR"/usr/plugins/networkinformation
rm -f "$APPDIR"/usr/lib/{libtiff.so*,libjbig.so*,libLerc.so*,libdeflate.so*,libmng.so*,liblcms2.so*}
# every remaining file must still find its libraries (bundled or system)
missing=$(find "$APPDIR/usr" -type f \( -name '*.so*' -o -path '*/bin/*' \) | while read -r f; do
    LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$f" 2>/dev/null | grep 'not found' | sed "s|^|$f: |" || true; done)
if [ -n "$missing" ]; then
    echo "Missing libraries:"; echo "$missing"; exit 1
fi

# ---------------------------------------------------------------- licenses (0.40)
# Every library and plugin the AppImage carries must have its license text in resources/licenses
# (the program shows them under Help > Third-Party Licenses); they also go into the AppImage.
# New libraries: run tools/collect-licenses.py <AppDir> resources/licenses and rebuild.
python3 -I "$ROOT/tools/collect-licenses.py" --check "$APPDIR" "$ROOT/resources/licenses" || exit 1
mkdir -p "$APPDIR/usr/share/doc/$APP_ID"
rm -rf "$APPDIR/usr/share/doc/$APP_ID/licenses"
cp -r "$ROOT/resources/licenses" "$APPDIR/usr/share/doc/$APP_ID/licenses"
cp "$ROOT/LICENSE" "$APPDIR/usr/share/doc/$APP_ID/LICENSE"
cp "$ROOT/THIRD-PARTY.md" "$APPDIR/usr/share/doc/$APP_ID/THIRD-PARTY.md"

# ---------------------------------------------------------------- AppImage
OUT="$BUILD/$APP_NAME-$VERSION${LABEL:+-$LABEL}-x86_64.AppImage"
rm -f "$OUT"
ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" --no-appstream \
    --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    --runtime-file "$TOOLS/runtime-x86_64" "$APPDIR" "$OUT"
echo
echo "Built: $OUT"
