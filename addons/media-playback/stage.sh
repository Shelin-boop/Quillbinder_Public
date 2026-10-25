#!/bin/bash
# Puts the libraries of the add-on "Media Playback" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/build-addons.sh)
# Needs: the two libraries of plugins/media (built with the program), Qt Multimedia 6.4.2 of
# Ubuntu 24.04 (qt6-multimedia-dev; the same Qt as the AppImage), FFmpeg 6.1 set "base" built by
# tools/build-ffmpeg.sh into <build dir>/ffmpeg (or QB_FFMPEG_DIR=<prefix>), libva2.
# Since 1.1 (0.50) the FFmpeg here decodes free formats only; the full set is the add-on
# "Extra Video and Audio Formats" (addons/media-extra-formats).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$1
STAGE=$2
FFP=${QB_FFMPEG_DIR:-$BUILD/ffmpeg}
FF=$FFP/lib
SYS=/usr/lib/x86_64-linux-gnu
P=$STAGE/payload
if [ "$(cat "$FFP/set.txt" 2>/dev/null)" != base ]; then
    echo "$FFP is not the FFmpeg set \"base\" (tools/get-ffmpeg.sh builds it)" >&2
    exit 1
fi
mkdir -p "$P/plugins" "$P/media/lib/fallback" "$P/media/qtplugins/multimedia"
cp "$BUILD/tool-plugins/libqb-media.so" "$P/plugins/"
cp "$BUILD/tool-plugins/media/libqb-media-impl.so" "$P/media/"
cp -L "$SYS/libQt6Multimedia.so.6" "$P/media/lib/"
for l in libavutil.so.58 libswresample.so.4 libswscale.so.7 libavcodec.so.60 libavformat.so.60; do
    cp -L "$FF/$l" "$P/media/lib/"
done
cp -L "$SYS/libva.so.2" "$P/media/lib/fallback/"
for b in ffmpeg gstreamer; do
    cp "$SYS/qt6/plugins/multimedia/lib${b}mediaplugin.so" "$P/media/qtplugins/multimedia/"
done
strip --strip-unneeded "$P"/plugins/*.so "$P"/media/*.so "$P"/media/lib/*.so.* "$P"/media/lib/fallback/*.so.* \
      "$P"/media/qtplugins/multimedia/*.so 2>/dev/null || true

# the license notes, with the source of exactly these libraries (0.50)
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
read -r QTM_PKG QTM_VER QTM_FILE < <(python3 -I "$ROOT/tools/collect-sources.py" --name ubuntu "$SYS/libQt6Multimedia.so.6")
read -r VA_PKG VA_VER VA_FILE < <(python3 -I "$ROOT/tools/collect-sources.py" --name ubuntu "$SYS/libva.so.2")
FF_FILE=$(python3 -I "$ROOT/tools/collect-sources.py" --name ffmpeg)
VERSION=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$STAGE/manifest.json")
cp "$FFP/decoders.txt" "$P/media/licenses/ffmpeg-decoders.txt"
cat > "$P/media/licenses/README.txt" <<TEXT
Libraries in this add-on ("Media Playback" $VERSION)
================================================

The add-on's own code (plugins/libqb-media.so, media/libqb-media-impl.so) is part of
Quillbinder. The other libraries are separate shared libraries under their own licences;
each can be replaced by another build of the same version (same file name).
The source code of each is in the release "Third-party source code" of the project page:
$SRC_URL

media/lib/libQt6Multimedia.so.6, media/qtplugins/multimedia/*.so
    Qt Multimedia 6.4.2, as built by Ubuntu 24.04 (source package $QTM_PKG $QTM_VER).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: file $QTM_FILE of the release named above
    (also from Ubuntu 24.04: apt-get source $QTM_PKG=$QTM_VER).

media/lib/libavutil.so.58, libswresample.so.4, libswscale.so.7, libavcodec.so.60, libavformat.so.60
    FFmpeg 6.1.1, built for this add-on (set "base" of tools/build-ffmpeg.sh of the Quillbinder
    source): decoders for free formats only (the list: ffmpeg-decoders.txt), demuxers and
    parsers, no external libraries but zlib, with one change
    (tools/ffmpeg-patches/0001-stream-duration-from-file.patch).
    H.264, H.265, AAC and the other formats licensed under patents are in the separate add-on
    "Extra Video and Audio Formats".
    Licence: GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
    Source: file $FF_FILE of the release named above (the original
    FFmpeg 6.1.1 archive, the patch and the build scripts).

media/lib/fallback/libva.so.2
    libva of Ubuntu 24.04 (source package $VA_PKG $VA_VER), used only on a system that has
    no libva.so.2 of its own. Licence: MIT (libva-copyright.txt).
    Source: file $VA_FILE of the release named above.
TEXT
