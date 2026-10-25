Libraries in this add-on ("Media Playback" 1.0)
================================================

The add-on's own code (plugins/libqb-media.so, media/libqb-media-impl.so) is part of
Quillbinder. The other libraries are separate shared libraries under their own licences;
each can be replaced by another build of the same version (same file name).

media/lib/libQt6Multimedia.so.6, media/qtplugins/multimedia/*.so
    Qt Multimedia 6.4.2, as built by Ubuntu 24.04 (package qt6-multimedia 6.4.2-11build3).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: https://download.qt.io/archive/qt/6.4/6.4.2/submodules/ or "apt-get source qt6-multimedia" on Ubuntu 24.04.

media/lib/libavutil.so.58, libswresample.so.4, libswscale.so.7, libavcodec.so.60, libavformat.so.60
    FFmpeg 6.1.1, built for this add-on: decoders, demuxers and parsers only, no external
    libraries but zlib, with one change (tools/ffmpeg-patches/0001-stream-duration-from-file.patch
    of the Quillbinder source). The configure options are in tools/build-ffmpeg.sh of the
    Quillbinder source. Licence: GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
    Source: https://ffmpeg.org/releases/ffmpeg-6.1.1.tar.xz or "apt-get source ffmpeg" on Ubuntu 24.04,
    plus the patch named above.

media/lib/fallback/libva.so.2
    libva 2.20.0 of Ubuntu 24.04, used only on a system that has no libva.so.2 of its own.
    Licence: MIT (libva-copyright.txt).
