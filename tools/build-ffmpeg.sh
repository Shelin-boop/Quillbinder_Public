#!/bin/bash
# Builds the FFmpeg that the add-on "Media Playback" brings along:
# FFmpeg 6.1.1 (sonames avcodec 60, avformat 60, avutil 58, swresample 4, swscale 7 =
# those Qt Multimedia 6.4.2 of Ubuntu 24.04 was built against), shared, LGPL 2.1 or later,
# decoders, demuxers and parsers only, no external libraries but zlib.
#   tools/build-ffmpeg.sh <ffmpeg source folder> <prefix>      e.g. ... ffmpeg-6.1.1 build/ffmpeg
# Source: "apt-get source ffmpeg" on Ubuntu 24.04 (ffmpeg_6.1.1.orig.tar.xz) or ffmpeg.org.
# Needs nasm.
#
# Left out on purpose:
#  - the decoders wmapro and wmv3 (+ wmv3image): Qt Multimedia 6.4.2's FFmpeg backend
#    crashes when it finds a decoder for WMA Pro or WMV 9 but no encoders (it indexes an
#    empty list, qffmpegmediaformatinfo.cpp); WMA 1/2 and VC-1 stay
#  - hardware decoding (VA-API, VDPAU): software decoding is enough for the files of a
#    writing project and needs no drivers
#  - AV1 (needs libdav1d)
# Patches (tools/ffmpeg-patches, applied here):
#  - 0001: streams without a duration of their own (Matroska, WebM) take the duration of the
#    file; Qt Multimedia 6.4.2 reads stream durations only and could not seek in .mkv / .webm
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$1" && pwd)
PREFIX=$(mkdir -p "$2" && cd "$2" && pwd)
cd "$SRC"
for p in "$HERE"/ffmpeg-patches/*.patch; do
    if patch -p1 -N --dry-run -s < "$p" >/dev/null 2>&1; then
        patch -p1 -N -s < "$p"
    fi
done
./configure --prefix="$PREFIX" --enable-shared --disable-static --disable-programs --disable-doc \
    --disable-avdevice --disable-avfilter --disable-postproc --disable-network --disable-autodetect \
    --enable-zlib --disable-encoders --disable-muxers --disable-hwaccels --disable-indevs \
    --disable-outdevs --disable-filters --disable-protocols --enable-protocol=file \
    --disable-decoder=wmapro --disable-decoder=wmv3 --disable-decoder=wmv3image \
    --disable-debug --enable-pic
make -j"$(nproc)"
make install
