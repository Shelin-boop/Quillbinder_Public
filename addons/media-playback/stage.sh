#!/bin/bash
# Puts the libraries of the add-on "Media Playback" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/linux/build-addons.sh)
# Needs: the two libraries of plugins/media (built with the program), Qt Multimedia of The Qt Company's own
# build of the Qt the AppImage carries (0.80.0: Qt 6.12.0 in QB_QT_DIR, tools/get-qt.sh), FFmpeg 9.0 set "base"
# built by tools/build-ffmpeg.sh into <build dir>/ffmpeg (or QB_FFMPEG_DIR=<prefix>).
# Since 1.1 (0.50) the FFmpeg here decodes free formats only; the full set is the add-on
# "Extra Video and Audio Formats" (addons/media-extra-formats). Since 2.0 (0.80.0): Qt 6.12, whose own build has
# no GStreamer backend; Qt's FFmpeg backend also needs Qt Quick and Qt QML, which the AppImage does not carry.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$1
STAGE=$2
FFP=${QB_FFMPEG_DIR:-$BUILD/ffmpeg}
FF=$FFP/lib
QT_VERSION=$(sed -n 's/^set(QB_QT_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
QT=${QB_QT_DIR:-/opt/qt/$QT_VERSION/gcc_64}
P=$STAGE/payload
if [ "$(cat "$FFP/set.txt" 2>/dev/null)" != base ]; then
    echo "$FFP is not the FFmpeg set \"base\" (tools/get-ffmpeg.sh builds it)" >&2
    exit 1
fi
[ -f "$QT/lib/libQt6Multimedia.so.6" ] || { echo "no Qt Multimedia in $QT (tools/get-qt.sh)" >&2; exit 1; }
mkdir -p "$P/plugins" "$P/media/lib" "$P/media/qtplugins/multimedia"
cp "$BUILD/tool-plugins/libqb-media.so" "$P/plugins/"
cp "$BUILD/tool-plugins/media/libqb-media-impl.so" "$P/media/"
# Qt Multimedia and the Qt libraries its FFmpeg backend needs (the plugin finds them through its search path
# $ORIGIN/../../lib = media/lib); the AppImage already has Core, Gui, Network and DBus
QT_LIBS=(libQt6Multimedia.so.6 libQt6Concurrent.so.6 libQt6OpenGL.so.6 libQt6Qml.so.6 libQt6QmlMeta.so.6
         libQt6QmlModels.so.6 libQt6QmlWorkerScript.so.6 libQt6Quick.so.6)
for l in "${QT_LIBS[@]}"; do
    cp -L "$QT/lib/$l" "$P/media/lib/"
done
for l in libavutil.so.61 libswresample.so.7 libswscale.so.10 libavcodec.so.63 libavformat.so.63; do
    cp -L "$FF/$l" "$P/media/lib/"
done
cp "$QT/plugins/multimedia/libffmpegmediaplugin.so" "$P/media/qtplugins/multimedia/"
strip --strip-unneeded "$P"/plugins/*.so "$P"/media/*.so "$P"/media/lib/*.so.* \
      "$P"/media/qtplugins/multimedia/*.so 2>/dev/null || true
# every library the add-on brings must find what it needs here or in a system / AppImage library
for f in "$P"/media/lib/*.so.* "$P"/media/qtplugins/multimedia/*.so "$P"/media/*.so; do
    for need in $(readelf -d "$f" | sed -n 's/.*NEEDED.*\[\(libQt6[^]]*\)\]/\1/p'); do
        case " ${QT_LIBS[*]} libQt6Core.so.6 libQt6Gui.so.6 libQt6Network.so.6 libQt6DBus.so.6 libQt6Widgets.so.6 " in
            *" $need "*) ;;
            *) echo "$(basename "$f") needs $need, which neither the add-on nor the AppImage carries" >&2; exit 1 ;;
        esac
    done
done

# the license notes, with the source of exactly these libraries (0.50)
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
QT_FILE=$(python3 -I "$ROOT/tools/collect-sources.py" --name qt)
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

media/lib/libQt6Multimedia.so.6, libQt6Concurrent.so.6, libQt6OpenGL.so.6, libQt6Qml.so.6,
libQt6QmlMeta.so.6, libQt6QmlModels.so.6, libQt6QmlWorkerScript.so.6, libQt6Quick.so.6,
media/qtplugins/multimedia/libffmpegmediaplugin.so
    Qt $QT_VERSION (Qt Multimedia and the parts of Qt its FFmpeg backend needs), The Qt Company's own
    build for Linux, unchanged; the same Qt as the AppImage.
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: file $QT_FILE of the release named above
    (also https://download.qt.io/official_releases/qt/).

media/lib/libavutil.so.61, libswresample.so.7, libswscale.so.10, libavcodec.so.63, libavformat.so.63
    FFmpeg 9.0.2, built for this add-on (set "base" of tools/build-ffmpeg.sh of the Quillbinder
    source): decoders for free formats only (the list: ffmpeg-decoders.txt), demuxers and
    parsers, no external libraries but zlib, with one change
    (tools/ffmpeg-patches/0001-stream-duration-from-file.patch).
    H.264, H.265, AAC and the other formats licensed under patents are in the separate add-on
    "Extra Video and Audio Formats".
    Licence: GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
    Source: file $FF_FILE of the release named above (the original
    FFmpeg 9.0.2 archive, the patch and the build scripts).
TEXT
