#!/bin/bash
# Builds an FFmpeg for the media add-ons:
# FFmpeg 6.1.1 (sonames avcodec 60, avformat 60, avutil 58, swresample 4, swscale 7 =
# those Qt Multimedia 6.4.2 of Ubuntu 24.04 was built against), shared, LGPL 2.1 or later,
# decoders, demuxers and parsers only, no external libraries but zlib.
#   tools/build-ffmpeg.sh <ffmpeg source folder> <prefix> [base|full]
#     e.g. ... ffmpeg-6.1.1 build/ffmpeg base         (the default)
# Source: "apt-get source ffmpeg" on Ubuntu 24.04 (ffmpeg_6.1.1.orig.tar.xz) or ffmpeg.org.
# Needs nasm. Builds outside the source folder (<prefix>.build), so one source serves both sets.
#
# Two sets (0.50):
#  - base: the add-on "Media Playback" (1.1). Only the decoders listed in BASE_DECODERS below:
#    formats that no patent pool licenses (free formats, or formats whose patents have run out):
#    PCM/WAV, common ADPCM, DSD, FLAC, ALAC, WavPack, TTA, Monkey's Audio, MPEG audio layers
#    I to III (MP1, MP2, MP3), Vorbis, Opus, Speex, GSM; VP8, VP9, Theora (VP3), Dirac, FFV1,
#    HuffYUV, Ut Video, MagicYUV, raw video, QuickTime RLE, Microsoft RLE, ZLIB/ZMBV; the picture
#    formats of cover art (JPEG, PNG, APNG, GIF, WebP, BMP); text subtitles.
#    This list is a careful choice, not a legal opinion: when in doubt a format was left out.
#  - full: the add-on "Extra Video and Audio Formats". Every decoder of FFmpeg 6.1.1 but those
#    left out below: also H.264, H.265 (HEVC), MPEG-1/2/4 video, VC-1, AAC, AC-3, E-AC-3, DTS,
#    WMA, AMR, ProRes ... Kept apart, so that it can be left out without touching the rest.
#
# Left out of both on purpose:
#  - the decoders wmapro and wmv3 (+ wmv3image): Qt Multimedia 6.4.2's FFmpeg backend
#    crashes when it finds a decoder for WMA Pro or WMV 9 but no encoders (it indexes an
#    empty list, qffmpegmediaformatinfo.cpp); WMA 1/2 and VC-1 stay (full set)
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
VARIANT=${3:-base}

BASE_DECODERS=(
    # PCM, ADPCM, DSD
    pcm_alaw pcm_bluray pcm_dvd pcm_f16le pcm_f24le pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_lxf
    pcm_mulaw pcm_s16be pcm_s16be_planar pcm_s16le pcm_s16le_planar pcm_s24be pcm_s24daud pcm_s24le
    pcm_s24le_planar pcm_s32be pcm_s32le pcm_s32le_planar pcm_s64be pcm_s64le pcm_s8 pcm_s8_planar
    pcm_sga pcm_u16be pcm_u16le pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 pcm_vidc
    adpcm_ms adpcm_ima_wav adpcm_ima_qt adpcm_g722 adpcm_g726 adpcm_g726le adpcm_yamaha adpcm_swf
    dsd_lsbf dsd_lsbf_planar dsd_msbf dsd_msbf_planar
    # lossless audio
    flac alac wavpack tta ape
    # MPEG-1/2 audio layers I to III
    mp1 mp1float mp2 mp2float mp3 mp3float mp3adu mp3adufloat
    # Xiph and others
    vorbis opus speex gsm gsm_ms
    # video
    vp8 vp9 vp3 theora dirac ffv1 ffvhuff huffyuv utvideo magicyuv rawvideo qtrle msrle zlib zmbv
    # pictures (cover art)
    mjpeg png apng gif webp bmp
    # text subtitles
    ass subrip webvtt text movtext
)

cd "$SRC"
for p in "$HERE"/ffmpeg-patches/*.patch; do
    if patch -p1 -N --dry-run -s < "$p" >/dev/null 2>&1; then
        patch -p1 -N -s < "$p"
    fi
done

case "$VARIANT" in
base)
    DECODERS=(--disable-decoders)
    for d in "${BASE_DECODERS[@]}"; do DECODERS+=("--enable-decoder=$d"); done
    ;;
full)
    DECODERS=(--disable-decoder=wmapro --disable-decoder=wmv3 --disable-decoder=wmv3image)
    ;;
*)
    echo "unknown set: $VARIANT (base or full)" >&2
    exit 2
    ;;
esac

OBJ="$PREFIX.build"
rm -rf "$OBJ"
mkdir -p "$OBJ"
cd "$OBJ"
"$SRC/configure" --prefix="$PREFIX" --enable-shared --disable-static --disable-programs --disable-doc \
    --disable-avdevice --disable-avfilter --disable-postproc --disable-network --disable-autodetect \
    --enable-zlib --disable-encoders --disable-muxers --disable-hwaccels --disable-indevs \
    --disable-outdevs --disable-filters --disable-protocols --enable-protocol=file \
    "${DECODERS[@]}" \
    --disable-debug --enable-pic
make -j"$(nproc)"
make install
# what is in it, for the add-on's notes and the checks
sed -n 's/^.*&ff_\([a-z0-9_]*\)_decoder.*$/\1/p' libavcodec/codec_list.c | sort > "$PREFIX/decoders.txt"
echo "$VARIANT" > "$PREFIX/set.txt"
echo "FFmpeg ($VARIANT, $(wc -l < "$PREFIX/decoders.txt") decoders) -> $PREFIX"
