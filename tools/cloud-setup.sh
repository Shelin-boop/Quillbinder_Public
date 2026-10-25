#!/bin/bash
# Setup script for a Claude Code cloud environment (Ubuntu 24.04, runs as root):
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
# build: Qt 6.4.2 of Ubuntu 24.04 (the version the runtime u2404-qt6.4-r1 carries), hunspell, zlib;
# the last line: Qt plugins that packaging/build-appimage.sh puts into the AppImage (KDE file dialogs, WebP)
apt-get install -y -qq --no-install-recommends \
    build-essential cmake ninja-build pkg-config file \
    qt6-base-dev qt6-base-private-dev qt6-base-dev-tools qt6-svg-dev qt6-pdf-dev qt6-multimedia-dev \
    qt6-qpa-plugins qt6-wayland libqt6svg6 libxkbcommon-dev libhunspell-dev hunspell-en-us zlib1g-dev nasm \
    qt6-xdgdesktopportal-platformtheme qt6-image-formats-plugins
# tests: X server, window tools, the fonts the tests ask for, PDF/EPUB checkers, sound
# (gstreamer1.0-libav: the GStreamer run of qb_m11_tests plays H.264)
apt-get install -y -qq --no-install-recommends \
    xvfb xauth xdotool imagemagick poppler-utils qpdf python3 \
    fonts-dejavu fonts-crosextra-caladea fonts-crosextra-carlito fonts-freefont-ttf fonts-liberation \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-libav pulseaudio pulseaudio-utils
# optional, large: epubcheck (EPUB check) and LibreOffice (Word files) for the compile checks
if [ "${QB_FULL_SETUP:-0}" = 1 ]; then
    apt-get install -y -qq --no-install-recommends default-jre-headless epubcheck libreoffice-writer-nogui
fi
echo "setup done"
