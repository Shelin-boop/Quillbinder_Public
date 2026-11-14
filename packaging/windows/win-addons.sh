#!/usr/bin/env bash
# Builds the Windows versions of the add-ons that carry libraries (port, W2, 2026-11-07).
# Runs in Git Bash on the Windows runner (packaging/windows/win-ci.sh) after the build.
#
#   packaging/windows/win-addons.sh <build dir> <Qt dir> <output dir>
#
# - Writing tools and importers (addons/*/plugins.txt, import-web): their DLLs from
#   <build>/tool-plugins (libqb-x.so in plugins.txt is qb-x.dll here).
# - Media Playback: qb-media.dll, qb-media-impl.dll, Qt Multimedia of the Qt the program was built
#   with, and Qt's Windows backend (the decoders of Windows, no FFmpeg; see
#   plugins/media/MediaEngineWin.cpp).
# - Extra Video and Audio Formats (W3): Qt's FFmpeg backend and the FFmpeg DLLs The Qt Company
#   builds for Qt Multimedia; refused when FFmpeg reports a GPL or non-free build.
# - Speech Transcription (W4c, 2026-11-12): qb-speech.dll, qb-speech-impl.dll, Qt Multimedia, whisper.cpp of
#   packaging/windows/win-whisper.sh (QB_WHISPER_DIR; the processor only).
# Every package with libraries gets "platform": "windows-x64" and the file name <id>-windows.qbpkg.
# Add-ons without libraries (themes, templates, compile formats; W3) are built here too, as on Linux
# (templates from their outlines with qb_template), without a platform: <id>.qbpkg.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$1
QT=$2
OUT=$3
PY=$(command -v python3 || command -v python)
mkdir -p "$OUT"
STAGES="$OUT/.stage"
rm -rf "$STAGES"

pack() { # <id> <stage dir>
    "$PY" "$ROOT/tools/make-package.py" addon --platform windows-x64 "$2" "$OUT/$1-windows.qbpkg"
}

stage_copy() { # <addon dir> <stage dir>
    mkdir -p "$2"
    cp "$1/manifest.json" "$2/"
    if [ -d "$1/payload" ]; then cp -r "$1/payload" "$2/"; else mkdir -p "$2/payload"; fi
}

# ---- writing tools and the Word importer
for dir in "$ROOT"/addons/*/; do
    id=$(basename "$dir")
    [ -f "$dir/plugins.txt" ] || continue
    stage="$STAGES/$id"
    stage_copy "$dir" "$stage"
    mkdir -p "$stage/payload/plugins"
    while read -r lib; do
        lib=${lib%$'\r'}
        [ -z "$lib" ] && continue
        dll=${lib#lib}
        dll=${dll%.so}.dll
        cp "$BUILD/tool-plugins/$dll" "$stage/payload/plugins/"
    done < "$dir/plugins.txt"
    pack "$id" "$stage"
done

# ---- web page import: the plugin only (Qt's TLS plugin for Windows comes with the program)
id=import-web
stage="$STAGES/$id"
mkdir -p "$stage/payload/plugins" "$stage/payload/licenses"
cp "$ROOT/addons/$id/manifest.json" "$stage/"
cp "$BUILD/tool-plugins/qb-import-web.dll" "$stage/payload/plugins/"
cat > "$stage/payload/licenses/README.txt" <<'TEXT'
Files in this add-on ("Web Page Import", Windows)
=================================================

plugins/qb-import-web.dll is part of Quillbinder (fetching, the article finder, the window).
Secure (https) pages are fetched with Qt's TLS plugin for Windows (Schannel), which comes with
the program itself.
TEXT
pack "$id" "$stage"

# ---- what Qt's media plugins need (W4a, Qt 6.12: on Linux its FFmpeg backend also needs Qt Quick, Qml and OpenGL);
# into the results beside the add-ons, for W4c
{
    echo "dumpbin: $(command -v dumpbin.exe || command -v dumpbin || echo 'not found')"
    for f in "$QT"/plugins/multimedia/ffmpegmediaplugin.dll "$QT"/plugins/multimedia/windowsmediaplugin.dll "$QT/bin/Qt6Multimedia.dll"; do
        echo "== $(basename "$f")"
        # (options with "-": Git Bash's MSYS turns "/nologo" into a path)
        dumpbin.exe -nologo -dependents "$(cygpath -w "$f")" 2>&1 | tr -d '\r' | grep -iE '\.dll|error|warning' | sed 's/^ */   /' | head -40
    done
} > "$(dirname "$OUT")/media-dependents.txt" 2>&1 || true

# ---- Media Playback
id=media-playback
stage="$STAGES/$id"
P="$stage/payload"
mkdir -p "$P/plugins" "$P/media/qtplugins/multimedia" "$P/media/licenses"
cp "$BUILD/tool-plugins/qb-media.dll" "$P/plugins/"
cp "$BUILD/tool-plugins/media/qb-media-impl.dll" "$P/media/"
cp "$QT/bin/Qt6Multimedia.dll" "$P/media/"
cp "$QT/plugins/multimedia/windowsmediaplugin.dll" "$P/media/qtplugins/multimedia/"
cp "$ROOT/addons/media-playback/payload/media/licenses/Qt-LGPL-3.0.txt" \
   "$ROOT/addons/media-playback/payload/media/licenses/GPL-3.0.txt" "$P/media/licenses/"
QTVER=$(basename "$(dirname "$QT")")
cat > "$P/media/licenses/README.txt" <<TEXT
Libraries in this add-on ("Media Playback", Windows)
==================================================

The add-on's own code (plugins/qb-media.dll, media/qb-media-impl.dll) is part of Quillbinder.

media/Qt6Multimedia.dll, media/qtplugins/multimedia/windowsmediaplugin.dll
    Qt Multimedia $QTVER, as published by The Qt Company for Windows (MSVC, 64 bit).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    The source code is published by the Qt Project (https://download.qt.io/official_releases/qt/).
    Each library can be replaced by another build of the same version (same file name).

Audio and video are decoded by Windows itself (Media Foundation): this add-on carries no
decoders of its own.
TEXT
"$PY" - "$ROOT/addons/media-playback/manifest.json" "$stage/manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
m["description"] = ("Plays audio and video files of the binder inside the program: play controls in the editor's "
                    "footer bar, Navigate > Media with keyboard shortcuts (Ctrl+Return plays or pauses while you type "
                    "in the other editor), pause and rewind for transcription, playing speed, timestamps. Brings Qt "
                    "Multimedia; the files are decoded by Windows itself (MP3, AAC, H.264, WMA, WAV, FLAC ... whatever "
                    "Windows plays; more formats come with codec extensions from the Microsoft Store).")
m["whatsNew"] = ["First version for Windows: decoding by Windows (Media Foundation)"]
json.dump(m, open(sys.argv[2], "w", encoding="utf-8"), indent=2, ensure_ascii=False)
PY
pack "$id" "$stage"

# ---- Extra Video and Audio Formats: Qt's FFmpeg (W3)
id=media-extra-formats
stage="$STAGES/$id"
E="$stage/payload/media-extra"
mkdir -p "$E/qtplugins/multimedia" "$E/lib" "$E/licenses"
cp "$QT/plugins/multimedia/ffmpegmediaplugin.dll" "$E/qtplugins/multimedia/"
for lib in avcodec avformat avutil swresample swscale; do
    f=$(ls "$QT/bin/" | grep -iE "^$lib-[0-9]+\.dll$" | head -1)
    [ -n "$f" ] || { echo "missing $lib in $QT/bin"; exit 1; }
    cp "$QT/bin/$f" "$E/lib/"
done
# what FFmpeg says about itself: version, license, configuration (the LGPL check)
"$PY" - "$(cygpath -w "$E/lib")" > "$E/licenses/ffmpeg-build.txt" <<'PY'
import ctypes, glob, os, sys
d = sys.argv[1]
os.add_dll_directory(d)
def lib(prefix):
    return ctypes.CDLL(glob.glob(os.path.join(d, prefix + "-*.dll"))[0])
avutil, avcodec, avformat = lib("avutil"), lib("avcodec"), lib("avformat")
for f in (avutil.av_version_info, avcodec.avcodec_license, avcodec.avcodec_configuration):
    f.restype = ctypes.c_char_p
print("FFmpeg version:", avutil.av_version_info().decode())
print("License:", avcodec.avcodec_license().decode())
print("Configuration:", avcodec.avcodec_configuration().decode())
PY
cat "$E/licenses/ffmpeg-build.txt"
if ! grep -q '^License: LGPL' "$E/licenses/ffmpeg-build.txt" || grep -qE -- '--enable-(gpl|nonfree|version3)' "$E/licenses/ffmpeg-build.txt"; then
    echo "FFmpeg of Qt is not a plain LGPL 2.1 build: add-on not made"
    exit 1
fi
ffver=$(sed -n 's/^FFmpeg version: //p' "$E/licenses/ffmpeg-build.txt" | tr -d '\r')
cp "$ROOT/addons/media-extra-formats/payload/media-extra/licenses/FFmpeg-LGPL-2.1.txt" "$E/licenses/"
cp "$ROOT/addons/media-playback/payload/media/licenses/Qt-LGPL-3.0.txt" \
   "$ROOT/addons/media-playback/payload/media/licenses/GPL-3.0.txt" "$E/licenses/"
cat > "$E/licenses/README.txt" <<TEXT
Libraries in this add-on ("Extra Video and Audio Formats", Windows)
==================================================================

lib/avcodec, avformat, avutil, swresample, swscale (*.dll)
    FFmpeg $ffver, as built by The Qt Company for Qt Multimedia $QTVER (installed with Qt's
    "qtmultimedia" module for MSVC 2022, 64 bit), unchanged. Licence: GNU LGPL 2.1 or later
    (FFmpeg-LGPL-2.1.txt). How it was built (FFmpeg's own report): ffmpeg-build.txt.
    Source code: the release "Third-party source code" of Quillbinder; FFmpeg's own sources:
    https://ffmpeg.org and https://github.com/FFmpeg/FFmpeg. Each DLL can be replaced by another
    build of the same FFmpeg version (same file name).

qtplugins/multimedia/ffmpegmediaplugin.dll
    Qt Multimedia's FFmpeg backend, Qt $QTVER. Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which
    refers to GPL-3.0.txt).

Some formats this add-on plays (H.264, H.265, AAC and others) are covered by patents in some
countries; whether a patent license is needed to use them depends on the country.
TEXT
"$PY" - "$ROOT/addons/media-extra-formats/manifest.json" "$stage/manifest.json" "$ffver" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
m["description"] = ("Lets the add-on \"Media Playback\" (2.0 or newer) play with FFmpeg: besides what Windows plays, "
                    "also Ogg Vorbis, Opus, WebM, Matroska and many more, including formats that are licensed under "
                    "patents (H.264, H.265 (HEVC), AAC, AC-3 ...). Brings FFmpeg " + sys.argv[3] + " (LGPL) as built by "
                    "The Qt Company for Qt Multimedia, and Qt's FFmpeg backend. While it is installed and switched on, "
                    "it is used instead of the decoders of Windows. Kept apart so that it can be left out: everything "
                    "else works without it. Whether a patent license is needed to use these formats depends on the country.")
m["whatsNew"] = ["First version for Windows: Qt Multimedia's FFmpeg backend"]
json.dump(m, open(sys.argv[2], "w", encoding="utf-8"), indent=2, ensure_ascii=False)
PY
pack "$id" "$stage"
# both media add-ons in one step, as media-all-formats.qbpkg on Linux
"$PY" "$ROOT/tools/make-package.py" bundle --id media-all-formats --name "Media Playback and Extra Formats" --version 1.0 \
    --description "Audio and video in the editor, in the commonly used formats, in one package: installs the add-ons \"Media Playback\" (the decoders of Windows) and \"Extra Video and Audio Formats\" (FFmpeg, also formats licensed under patents). Whether a patent license is needed to use those formats depends on the country." \
    --whats-new "Both media add-ons in one package (Windows)" \
    "$OUT/media-playback-windows.qbpkg" "$OUT/media-extra-formats-windows.qbpkg" "$OUT/media-all-formats-windows.qbpkg"

# ---- Speech Transcription (W4c, 2026-11-12): the tool, its engine, whisper.cpp of packaging/windows/win-whisper.sh
# (QB_WHISPER_DIR; the processor only), Qt Multimedia for the microphone; Qt's TLS plugin for the model download
# comes with the program (Schannel). Left out, with a line in the log, when whisper.cpp or the engine was not built.
id=speech-transcription
W=${QB_WHISPER_DIR:-}
if [ -n "$W" ] && [ -f "$W/bin/whisper.dll" ] && [ -f "$BUILD/tool-plugins/speech/qb-speech-impl.dll" ]; then
    stage="$STAGES/$id"
    stage_copy "$ROOT/addons/$id" "$stage"
    P="$stage/payload"
    mkdir -p "$P/plugins" "$P/speech/lib" "$P/speech/samples" "$P/speech/licenses"
    cp "$BUILD/tool-plugins/qb-speech.dll" "$P/plugins/"
    cp "$BUILD/tool-plugins/speech/qb-speech-impl.dll" "$P/speech/"
    cp "$QT/bin/Qt6Multimedia.dll" "$P/speech/"
    cp "$W/bin/whisper.dll" "$W/bin/ggml.dll" "$W/bin/ggml-base.dll" "$W"/bin/ggml-cpu-*.dll "$P/speech/lib/"
    cp "$W/samples/jfk.wav" "$P/speech/samples/"
    cp "$W/LICENSE-whisper.cpp.txt" "$P/speech/licenses/whisper.cpp-MIT.txt"
    QTVER=$(basename "$(dirname "$QT")")
    WVER=$(cut -d' ' -f1-2 "$W/version.txt")
    cat > "$P/speech/licenses/README.txt" <<TEXT
Libraries in this add-on ("Speech Transcription", Windows)
=========================================================

The add-on's own code (plugins/qb-speech.dll, speech/qb-speech-impl.dll) is part of Quillbinder.
The other libraries are separate DLLs under their own licences; each can be replaced by another
build of the same version (same file name).

speech/lib/whisper.dll, ggml.dll, ggml-base.dll, ggml-cpu-*.dll
    whisper.cpp $WVER (github.com/ggml-org/whisper.cpp), speech recognition with OpenAI's
    Whisper models, built with Microsoft Visual C++ by packaging/windows/win-whisper.sh of the
    Quillbinder source: one compute module per processor generation, the best one is chosen
    when the program starts. Licence: MIT (whisper.cpp-MIT.txt).

speech/samples/jfk.wav
    The test recording of the model check: from President John F. Kennedy's inaugural address
    (1961), a work of the United States government (public domain), as whisper.cpp ships it.

speech/Qt6Multimedia.dll
    Qt Multimedia $QTVER (the microphone), as published by The Qt Company for Windows (MSVC,
    64 bit), unchanged; the same Qt as the program. Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which
    refers to GPL-3.0.txt). The source code is published by the Qt Project
    (https://download.qt.io/official_releases/qt/).

The models are not part of the add-on: the program downloads the one the writer chooses from
whisper.cpp's model files (huggingface.co/ggerganov/whisper.cpp), OpenAI's Whisper models
converted for whisper.cpp. Licence of the models: MIT.
TEXT
    "$PY" - "$ROOT/addons/$id/manifest.json" "$stage/manifest.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
m["description"] = ("Speech becomes text, on this computer only: a microphone button in the main toolbar starts and stops "
                    "a recording (the arrow beside it chooses the microphone), and what is said is written into a new "
                    "transcription file, listed in the inspector's tab \"Transcriptions\", where it can be edited and "
                    "copied. Or record only, using little power, and let the text be written later with Transcribe. "
                    "Recognition by whisper.cpp on the processor; the model (Small, Medium, Large v3 or Large v3 Turbo) "
                    "is chosen and downloaded once under File > Options > Speech, checked, and then used without any "
                    "connection. A main language for the text and a fallback language that is also spoken: with English "
                    "as the main language, what is said in the other language is written in English. Brings Qt "
                    "Multimedia for the microphone.")
m["whatsNew"] = ["First version for Windows: recognition on the processor (graphics cards later)"]
json.dump(m, open(sys.argv[2], "w", encoding="utf-8"), indent=2, ensure_ascii=False)
PY
    pack "$id" "$stage"
else
    echo "speech-transcription: left out (whisper.cpp in QB_WHISPER_DIR or the speech engine is missing)"
fi

# ---- add-ons without libraries (W3): as packaging/linux/build-addons.sh makes them on Linux
for dir in "$ROOT"/addons/*/; do
    id=$(basename "$dir")
    [ -f "$dir/plugins.txt" ] || [ -x "$dir/stage.sh" ] || [ -f "$dir/stage.sh" ] && continue
    case "$id" in import-web|media-*|tool-*) continue ;; esac
    stage="$STAGES/$id"
    stage_copy "$dir" "$stage"
    tpl() { ( cd "$BUILD" && PATH="$(cygpath -u "$QT")/bin:$PATH" QT_QPA_PLATFORM=offscreen ./qb_template.exe "$(cygpath -w "$1")" "$(cygpath -w "$2")" ); }
    if [ -f "$dir/outline.json" ]; then
        mkdir -p "$stage/payload/templates"
        tpl "$dir/outline.json" "$stage/payload/templates/${id#template-}.zip"
    fi
    if [ -d "$dir/outlines" ]; then
        mkdir -p "$stage/payload/templates"
        for outline in "$dir"/outlines/*.json; do
            tpl "$outline" "$stage/payload/templates/$(basename "${outline%.json}").zip"
        done
    fi
    "$PY" "$ROOT/tools/make-package.py" addon "$stage" "$OUT/$id.qbpkg"
done

rm -rf "$STAGES"
ls -l "$OUT"
