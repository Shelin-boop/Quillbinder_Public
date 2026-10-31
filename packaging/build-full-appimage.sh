#!/bin/bash
# Builds the full version of the AppImage (0.60): the AppImage of packaging/build-appimage.sh with every
# add-on package inside. At the first start of this version the add-ons are installed (as with
# File > Install Update or Add-on), so the program comes with audio and video in every format, the
# themes, templates, writing tools and importers, and all the libraries they need.
#
#   packaging/build-full-appimage.sh [AppImage build dir] [add-on packages dir]
#
# Needs: <build dir>/<Name>-<version>-<label>-x86_64.AppImage (packaging/build-appimage.sh, which also
# fetches appimagetool and the runtime) and one <id>.qbpkg per folder of addons/ in the packages dir
# (packaging/build-addons.sh; the media add-ons need tools/get-ffmpeg.sh).
# Result: <build dir>/<Name>-<version>-full-<label>-x86_64.AppImage
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=$(cd "${1:-$ROOT/build-appimage}" && pwd)
ADDONS=$(cd "${2:-$ROOT/dist}" && pwd)
TOOLS=${TOOLS:-$ROOT/packaging/tools}
APP_ID=$(sed -n 's/^set(APP_ID *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
APP_NAME=$(sed -n 's/^set(APP_NAME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
VERSION=$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
LABEL=$(sed -n 's/^set(APP_VERSION_LABEL *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
IN="$BUILD/$APP_NAME-$VERSION${LABEL:+-$LABEL}-x86_64.AppImage"
OUT="$BUILD/$APP_NAME-$VERSION-full${LABEL:+-$LABEL}-x86_64.AppImage"
[ -f "$IN" ] || { echo "no AppImage: $IN (packaging/build-appimage.sh)" >&2; exit 1; }
for t in appimagetool-x86_64.AppImage runtime-x86_64; do
    [ -f "$TOOLS/$t" ] || { echo "missing $TOOLS/$t (packaging/build-appimage.sh fetches it)" >&2; exit 1; }
done
# every add-on of the source, nothing else (not the program update, not the media bundle, which
# holds two of these packages again)
packages=()
for d in "$ROOT"/addons/*/; do
    id=$(basename "$d")
    [ -f "$ADDONS/$id.qbpkg" ] || { echo "missing add-on package: $ADDONS/$id.qbpkg" >&2; exit 1; }
    packages+=("$ADDONS/$id.qbpkg")
done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
(cd "$WORK" && "$IN" --appimage-extract > /dev/null)
APPDIR="$WORK/squashfs-root"
FULL="$APPDIR/usr/share/$APP_ID/full-addons"
mkdir -p "$FULL"
cp "${packages[@]}" "$FULL/"
cat > "$FULL/README.txt" <<TEXT
$APP_NAME $VERSION${LABEL:+ $LABEL}, full version

This AppImage is $APP_NAME with all its add-ons and the libraries they need included: audio and
video in every format (Media Playback with Qt Multimedia and FFmpeg, also the formats licensed under
patents), the extra themes, the project templates, the compile format, the scriptwriting pack, the
writing tools (Name Generator, Scratch Pad, Text Statistics) and the importers of web pages and old
Word files. They are installed at the first start of this version, as File > Install Update or
Add-on would install them, and can be switched off or removed in Help > Add-on Manager like any
other add-on. Whether a patent license is needed to use some video and audio formats depends on the
country. The licenses of every part: Help > Third-Party Licenses and the add-ons' own license files.
TEXT
# the start script: install the packages once per version, then start the program as before
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<SCRIPT
#!/bin/sh
# $APP_NAME, full version: installs the add-ons inside this AppImage at the first start of
# version $VERSION (packaging/build-full-appimage.sh), then starts the program.
HERE=\$(dirname "\$(readlink -f "\$0")")
BIN="\$HERE/usr/bin/$APP_ID"
MARK="\${XDG_DATA_HOME:-\$HOME/.local/share}/$APP_NAME/full-addons-installed-$VERSION"
if [ ! -e "\$MARK" ]; then
    ok=1
    for p in "\$HERE"/usr/share/$APP_ID/full-addons/*.qbpkg; do
        "\$BIN" --install "\$p" > /dev/null 2>&1 || ok=0
    done
    if [ \$ok = 1 ]; then
        mkdir -p "\$(dirname "\$MARK")" && : > "\$MARK"
    fi
fi
exec "\$BIN" "\$@"
SCRIPT
chmod +x "$APPDIR/AppRun"
# the extracted folders can come out readable by their owner only; appimagetool refuses those
chmod -R a+rX "$APPDIR"
rm -f "$OUT"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" --no-appstream \
    --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    --runtime-file "$TOOLS/runtime-x86_64" "$APPDIR" "$OUT" > "$WORK/appimagetool.log" 2>&1 \
    || { tail -20 "$WORK/appimagetool.log"; exit 1; }
echo "Built: $OUT (${#packages[@]} add-ons inside)"
