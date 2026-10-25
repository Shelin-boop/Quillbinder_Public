#!/bin/bash
# Downloads the FFmpeg 6.1.1 source of Ubuntu 24.04 and builds it for the add-on
# "Media Playback" (tools/build-ffmpeg.sh says what is in it and why).
#   tools/get-ffmpeg.sh [prefix]          default: build/ffmpeg
# Takes about 5 to 10 minutes; needs nasm. Run it once; the prefix is reused.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${1:-$ROOT/build/ffmpeg}
WORK=$ROOT/build/ffmpeg-src
URL=http://archive.ubuntu.com/ubuntu/pool/universe/f/ffmpeg/ffmpeg_6.1.1.orig.tar.xz
SHA256=8684f4b00f94b85461884c3719382f1261f0d9eb3d59640a1f4ac0873616f968
if [ -f "$PREFIX/lib/libavcodec.so.60" ]; then
    echo "FFmpeg is there: $PREFIX"
    exit 0
fi
mkdir -p "$WORK"
cd "$WORK"
[ -f ffmpeg_6.1.1.orig.tar.xz ] || curl -fsSLO "$URL"
echo "$SHA256  ffmpeg_6.1.1.orig.tar.xz" | sha256sum -c -
rm -rf ffmpeg-6.1.1
tar xf ffmpeg_6.1.1.orig.tar.xz
"$ROOT/tools/build-ffmpeg.sh" "$WORK/ffmpeg-6.1.1" "$PREFIX"
echo "FFmpeg built: $PREFIX"
