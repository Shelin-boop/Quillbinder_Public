#!/bin/bash
# Puts the libraries of the add-on "Extra Video and Audio Formats" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/build-addons.sh)
# Needs: FFmpeg 6.1 set "full" built by tools/build-ffmpeg.sh into <build dir>/ffmpeg-full
# (or QB_FFMPEG_FULL_DIR=<prefix>). No code of its own: the add-on "Media Playback" (1.1 or
# newer) finds the libraries in media-extra/lib/ and loads them instead of its own (0.50).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$1
STAGE=$2
FFP=${QB_FFMPEG_FULL_DIR:-$BUILD/ffmpeg-full}
P=$STAGE/payload
if [ "$(cat "$FFP/set.txt" 2>/dev/null)" != full ]; then
    echo "$FFP is not the FFmpeg set \"full\" (tools/get-ffmpeg.sh builds it)" >&2
    exit 1
fi
mkdir -p "$P/media-extra/lib" "$P/media-extra/licenses"
for l in libavutil.so.58 libswresample.so.4 libswscale.so.7 libavcodec.so.60 libavformat.so.60; do
    cp -L "$FFP/lib/$l" "$P/media-extra/lib/"
done
strip --strip-unneeded "$P"/media-extra/lib/*.so.* 2>/dev/null || true
cp "$FFP/decoders.txt" "$P/media-extra/licenses/ffmpeg-decoders.txt"
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
FF_FILE=$(python3 -I "$ROOT/tools/collect-sources.py" --name ffmpeg)
cat > "$P/media-extra/licenses/README.txt" <<TEXT
Libraries in this add-on ("Extra Video and Audio Formats")
==========================================================

media-extra/lib/libavutil.so.58, libswresample.so.4, libswscale.so.7, libavcodec.so.60, libavformat.so.60
    FFmpeg 6.1.1, built for this add-on (set "full" of tools/build-ffmpeg.sh of the Quillbinder
    source): every decoder of FFmpeg 6.1.1 but WMA Pro and WMV 9 (the list:
    ffmpeg-decoders.txt), demuxers and parsers, no external libraries but zlib, with one change
    (tools/ffmpeg-patches/0001-stream-duration-from-file.patch). The add-on "Media Playback"
    (1.1 or newer) loads these libraries instead of its own while this add-on is installed and
    switched on. They can be replaced by another build of the same version (same file names).
    Licence: GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
    Source: file $FF_FILE of the release "Third-party source code"
    of the project page: $SRC_URL
    (the original FFmpeg 6.1.1 archive, the patch and the build scripts).

Formats licensed under patents
    Some formats these libraries decode (among them H.264, H.265, AAC, AC-3) are covered by
    patents in some countries. This add-on is offered separately so that it can be left out;
    whether a license is needed depends on where and how it is used.
TEXT
