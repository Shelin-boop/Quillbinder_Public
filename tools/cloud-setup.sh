#!/bin/bash
# Setup script for a Claude Code cloud environment and GitHub's container (Ubuntu 24.04, runs as root):
# everything needed to build Quillbinder and run its tests. Paste the line below
# into the environment's "Setup script" field (network access: Trusted):
#
#   bash tools/cloud-setup.sh
#
# (If the environment runs the script before the repository is there, paste the
# apt-get lines of this file instead.)
# FFmpeg for the add-on "Media Playback" is not built here (it takes several
# minutes): tools/get-ffmpeg.sh, once per session, when the media checks matter.
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# build: hunspell, zlib and what The Qt Company's own build of Qt needs (0.80.0: Qt 6.12.0, runtime u2404-qt6.12-r1,
# fetched by tools/get-qt.sh at the end of this script; Ubuntu's Qt 6.4.2 is no longer used)
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config file python3-pip xz-utils \
    libxkbcommon-dev libxkbcommon-x11-0 libhunspell-dev hunspell-en-us zlib1g-dev nasm \
    libgl-dev libegl-dev libgl1 libegl1 libopengl0 libfontconfig1-dev libfreetype-dev libdbus-1-3 libglib2.0-0 \
    libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 \
    libxcb-xkb1 libxcb-randr0 libxcb-xfixes0 libxcb-sync1 libxcb-shm0 libxcb1 libx11-xcb1 \
    libwayland-client0 libwayland-cursor0 libwayland-egl1 libssl3
# the add-on "Speech Transcription" (0.61): whisper.cpp's Vulkan module for graphics cards (tools/get-whisper.sh)
apt-get install -y -qq --no-install-recommends git libvulkan-dev glslc spirv-headers
# tests: X server, window tools, the fonts the tests ask for, PDF/EPUB checkers, sound, a Vulkan driver
# that runs on the processor (lavapipe: the graphics-card path of the speech add-on without a card)
# (gstreamer1.0-libav: the GStreamer run of qb_m11_tests plays H.264)
apt-get install -y -qq --no-install-recommends \
    xvfb xauth xdotool imagemagick poppler-utils qpdf python3 unzip \
    fonts-dejavu fonts-crosextra-caladea fonts-crosextra-carlito fonts-freefont-ttf fonts-liberation \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav pulseaudio pulseaudio-utils \
    mesa-vulkan-drivers
# optional, large: epubcheck (EPUB check) and LibreOffice (Word files) for the compile checks
if [ "${QB_FULL_SETUP:-0}" = 1 ]; then
    apt-get install -y -qq --no-install-recommends default-jre-headless epubcheck libreoffice-writer-nogui
fi
# Qt for the build (download.qt.io where it can be reached, else the private repository's copy)
bash "$(dirname "$0")/get-qt.sh"
echo "setup done"
