#!/bin/bash
# Downloads the FFmpeg 6.1.1 source of Ubuntu 24.04 and builds the two FFmpeg sets of the
# media add-ons (tools/build-ffmpeg.sh says what is in each and why):
#   base -> "Media Playback"                      (default prefix build/ffmpeg)
#   full -> "Extra Video and Audio Formats"       (default prefix build/ffmpeg-full)
#   tools/get-ffmpeg.sh [base prefix [full prefix]]
# Takes about 10 to 20 minutes for both; needs nasm. Run it once; finished sets are reused.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=${1:-${QB_FFMPEG_DIR:-$ROOT/build/ffmpeg}}
FULL=${2:-${QB_FFMPEG_FULL_DIR:-$ROOT/build/ffmpeg-full}}
WORK=$ROOT/build/ffmpeg-src
URL=http://archive.ubuntu.com/ubuntu/pool/universe/f/ffmpeg/ffmpeg_6.1.1.orig.tar.xz
SHA256=8684f4b00f94b85461884c3719382f1261f0d9eb3d59640a1f4ac0873616f968
need() {  # <prefix> <set>: missing, or another set (before 0.50 build/ffmpeg held every decoder)
    [ ! -f "$1/lib/libavcodec.so.60" ] || [ "$(cat "$1/set.txt" 2>/dev/null)" != "$2" ]
}
if ! need "$BASE" base && ! need "$FULL" full; then
    echo "FFmpeg is there: $BASE (base), $FULL (full)"
    exit 0
fi
mkdir -p "$WORK"
cd "$WORK"
[ -f ffmpeg_6.1.1.orig.tar.xz ] || curl -fsSLO "$URL"
echo "$SHA256  ffmpeg_6.1.1.orig.tar.xz" | sha256sum -c -
if [ ! -f ffmpeg-6.1.1/configure ]; then
    rm -rf ffmpeg-6.1.1
    tar xf ffmpeg_6.1.1.orig.tar.xz
fi
if need "$BASE" base; then
    rm -rf "$BASE"
    "$ROOT/tools/build-ffmpeg.sh" "$WORK/ffmpeg-6.1.1" "$BASE" base
fi
if need "$FULL" full; then
    rm -rf "$FULL"
    "$ROOT/tools/build-ffmpeg.sh" "$WORK/ffmpeg-6.1.1" "$FULL" full
fi
echo "FFmpeg built: $BASE (base), $FULL (full)"
