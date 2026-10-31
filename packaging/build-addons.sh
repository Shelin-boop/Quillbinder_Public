#!/bin/bash
# Builds the add-on packages in addons/ into <out>/*.qbpkg.
#   packaging/build-addons.sh [build dir] [output dir]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${1:-$ROOT/build}
OUT=${2:-$ROOT/dist}
mkdir -p "$OUT"
for dir in "$ROOT"/addons/*/; do
    id=$(basename "$dir")
    # project templates are written as outlines and turned into template files here
    if [ -f "$dir/outline.json" ]; then
        mkdir -p "$dir/payload/templates"
        QT_QPA_PLATFORM=offscreen "$BUILD/qb_template" "$dir/outline.json" \
            "$dir/payload/templates/${id#template-}.zip" 2>/dev/null
    fi
    # an add-on with several project templates keeps its outlines in outlines/
    if [ -d "$dir/outlines" ]; then
        mkdir -p "$dir/payload/templates"
        for outline in "$dir"/outlines/*.json; do
            QT_QPA_PLATFORM=offscreen "$BUILD/qb_template" "$outline" \
                "$dir/payload/templates/$(basename "${outline%.json}").zip" 2>/dev/null
        done
    fi
    # writing tools: the plugin libraries named in plugins.txt come from <build>/tool-plugins;
    # they are put into a copy of the add-on, so the source folder stays free of built files
    # add-ons that bring libraries of their own stage them with a script (addons/media-playback/stage.sh)
    if [ -x "$dir/stage.sh" ]; then
        stage="$OUT/.stage/$id"
        rm -rf "$stage"; mkdir -p "$stage"
        cp -r "$dir/manifest.json" "$stage/"
        [ -d "$dir/payload" ] && cp -r "$dir/payload" "$stage/" || mkdir -p "$stage/payload"
        "$dir/stage.sh" "$BUILD" "$stage"
        python3 "$ROOT/tools/make-package.py" addon "$stage" "$OUT/$id.qbpkg"
        rm -rf "$stage"
        continue
    fi
    if [ -f "$dir/plugins.txt" ]; then
        stage="$OUT/.stage/$id"
        rm -rf "$stage"; mkdir -p "$stage"
        cp -r "$dir/manifest.json" "$stage/"
        [ -d "$dir/payload" ] && cp -r "$dir/payload" "$stage/" || mkdir -p "$stage/payload"
        mkdir -p "$stage/payload/plugins"
        while read -r lib; do
            [ -z "$lib" ] && continue
            cp "$BUILD/tool-plugins/$lib" "$stage/payload/plugins/"
            strip "$stage/payload/plugins/$lib" 2>/dev/null || true
        done < "$dir/plugins.txt"
        python3 "$ROOT/tools/make-package.py" addon "$stage" "$OUT/$id.qbpkg"
        rm -rf "$stage"
        continue
    fi
    python3 "$ROOT/tools/make-package.py" addon "$dir" "$OUT/$id.qbpkg"
done
rm -rf "$OUT/.stage"
# One package for audio and video in every format (0.60): a bundle of "Media Playback" and
# "Extra Video and Audio Formats", installed in one step with File > Install Update or Add-on.
# The two add-ons stay separate inside it, so the patent-licensed formats can still be switched
# off or removed on their own in the Add-on Manager.
if [ -f "$OUT/media-playback.qbpkg" ] && [ -f "$OUT/media-extra-formats.qbpkg" ]; then
    python3 "$ROOT/tools/make-package.py" bundle --id media-all-formats --name "Media Playback, All Formats" \
        --version 1.0 \
        --description "Audio and video in the editor, in every format the program knows, in one package: installs the add-ons \"Media Playback\" (free formats) and \"Extra Video and Audio Formats\" (H.264, H.265, AAC and the other formats licensed under patents). Whether a patent license is needed to use those formats depends on the country." \
        --whats-new "Both media add-ons in one package" \
        "$OUT/media-playback.qbpkg" "$OUT/media-extra-formats.qbpkg" "$OUT/media-all-formats.qbpkg"
fi
