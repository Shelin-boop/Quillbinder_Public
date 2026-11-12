#!/bin/bash
# Puts the libraries of the add-on "Speech Transcription" (0.61) into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/linux/build-addons.sh)
# Needs: the two libraries of plugins/tools/speech-transcription (built with the program), whisper.cpp
# built by tools/get-whisper.sh into <build dir>/../build/whisper (or QB_WHISPER_DIR), Qt Multimedia and Qt's TLS
# plugin of The Qt Company's own build of the Qt the AppImage carries (0.80.0: Qt 6.12.0 in QB_QT_DIR; the
# AppImage has neither).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$1
STAGE=$2
W=${QB_WHISPER_DIR:-$ROOT/build/whisper}
QT_VERSION=$(sed -n 's/^set(QB_QT_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
QT=${QB_QT_DIR:-/opt/qt/$QT_VERSION/gcc_64}
P=$STAGE/payload
if [ ! -f "$W/lib/libwhisper.so.1" ] || [ ! -f "$BUILD/tool-plugins/speech/libqb-speech-impl.so" ]; then
    echo "speech-transcription: whisper.cpp or the speech engine is missing (tools/get-whisper.sh, then build again)" >&2
    exit 3
fi
mkdir -p "$P/plugins" "$P/speech/lib" "$P/speech/samples" "$P/speech/qtplugins/tls" "$P/speech/licenses"
cp "$BUILD/tool-plugins/libqb-speech.so" "$P/plugins/"
cp "$BUILD/tool-plugins/speech/libqb-speech-impl.so" "$P/speech/"
# whisper.cpp: the library, ggml, and its compute modules (one per processor generation, Vulkan)
for l in libwhisper.so.1 libggml.so.0 libggml-base.so.0; do
    cp -L "$W/lib/$l" "$P/speech/lib/"
done
cp "$W"/lib/libggml-cpu-*.so "$P/speech/lib/"
[ -f "$W/lib/libggml-vulkan.so" ] && cp "$W/lib/libggml-vulkan.so" "$P/speech/lib/"
cp "$W/samples/jfk.wav" "$P/speech/samples/"
cp -L "$QT/lib/libQt6Multimedia.so.6" "$QT/lib/libQt6Concurrent.so.6" "$P/speech/lib/"
cp "$QT/plugins/tls/libqopensslbackend.so" "$P/speech/qtplugins/tls/"
strip --strip-unneeded "$P"/plugins/*.so "$P"/speech/*.so "$P"/speech/lib/*.so* "$P"/speech/qtplugins/tls/*.so 2>/dev/null || true

# the license notes, with the source of exactly these libraries
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
QT_FILE=$(python3 -I "$ROOT/tools/collect-sources.py" --name qt)
VERSION=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$STAGE/manifest.json")
WHISPER=$(cat "$W/version.txt")
cp "$W/LICENSE-whisper.cpp.txt" "$P/speech/licenses/whisper.cpp-MIT.txt"
cat > "$P/speech/licenses/README.txt" <<TEXT
Libraries in this add-on ("Speech Transcription" $VERSION)
=====================================================

The add-on's own code (plugins/libqb-speech.so, speech/libqb-speech-impl.so) is part of
Quillbinder. The other libraries are separate shared libraries under their own licences;
each can be replaced by another build of the same version (same file name).

speech/lib/libwhisper.so.1, libggml.so.0, libggml-base.so.0, libggml-cpu-*.so, libggml-vulkan.so
    whisper.cpp $WHISPER (github.com/ggml-org/whisper.cpp), speech recognition with
    OpenAI's Whisper models, built by tools/get-whisper.sh of the Quillbinder source.
    Licence: MIT (whisper.cpp-MIT.txt).

speech/samples/jfk.wav
    The test recording of the model check: from President John F. Kennedy's inaugural address
    (1961), a work of the United States government (public domain), as whisper.cpp ships it.

speech/lib/libQt6Multimedia.so.6, libQt6Concurrent.so.6
    Qt Multimedia $QT_VERSION (the microphone) and Qt Concurrent, which it needs: The Qt Company's own
    build for Linux, unchanged; the same Qt as the AppImage.
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: file $QT_FILE of the release "Third-party source code" of the project page:
    $SRC_URL
    (also https://download.qt.io/official_releases/qt/).

speech/qtplugins/tls/libqopensslbackend.so
    Qt's TLS plugin for OpenSSL (Qt Network $QT_VERSION), The Qt Company's own build for Linux, for the
    download of a model over https. It uses the system's OpenSSL 3.
    Licence: GNU LGPL 3.0. Source: file $QT_FILE of the release named above.

The models are not part of the add-on: the program downloads the one the writer chooses from
whisper.cpp's model files (huggingface.co/ggerganov/whisper.cpp), OpenAI's Whisper models
converted for whisper.cpp. Licence of the models: MIT.
TEXT
