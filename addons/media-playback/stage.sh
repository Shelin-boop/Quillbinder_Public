#!/bin/bash
# Puts the libraries of the add-on "Media Playback" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/build-addons.sh)
# Needs: the two libraries of plugins/media (built with the program), Qt Multimedia 6.4.2 of
# Ubuntu 24.04 (qt6-multimedia-dev; the same Qt as the AppImage), FFmpeg 6.1 built by
# tools/build-ffmpeg.sh into <build dir>/ffmpeg (or QB_FFMPEG_DIR=<prefix>), libva2.
set -euo pipefail
BUILD=$1
STAGE=$2
FF=${QB_FFMPEG_DIR:-$BUILD/ffmpeg}/lib
SYS=/usr/lib/x86_64-linux-gnu
P=$STAGE/payload
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
