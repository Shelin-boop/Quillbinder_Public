#!/bin/bash
# The license files of Qt's own Linux build (0.80.0), for tools/collect-licenses.py --qt-licenses <dir>:
# Qt's attribution files (qt_attribution.json) and the license files they name, from the Qt Project's
# repositories on GitHub at the tag of QB_QT_VERSION (CMakeLists.txt), plus the license files of Qt PDF's parts
# (qtwebengine-chromium at the commit qtwebengine's tag builds) and of the ICU build Qt ships.
#
#   tools/get-qt-licenses.sh [<dir>]       (default build/qt-licenses)
#
# Only sparse checkouts (the files named above); GitHub is reachable from the cloud session.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^set(QB_QT_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
DIR=${1:-$ROOT/build/qt-licenses}
ICU_TAG=release-73-2   # the ICU Qt's Linux binaries carry (libicuuc.so.73.2)
mkdir -p "$DIR"

sparse() {  # <repo dir> <url> <ref> <patterns...>
    local d=$1 url=$2 ref=$3
    shift 3
    if [ ! -d "$d/.git" ]; then
        git clone -q --filter=blob:none --no-checkout --depth 1 --branch "$ref" "$url" "$d"
    fi
    git -C "$d" sparse-checkout set --no-cone "$@"
    git -C "$d" checkout -q
}

# the files the attribution files of a checkout name (license, copyright)
named() {  # <repo dir>
    python3 -I - "$1" <<'PY'
import json, os, sys
base = sys.argv[1]
out = set()
for dirpath, _, files in os.walk(base):
    if ".git" in dirpath or "qt_attribution.json" not in files:
        continue
    with open(os.path.join(dirpath, "qt_attribution.json"), encoding="utf-8", errors="replace") as f:
        data = json.loads(f.read(), strict=False)
    for item in data if isinstance(data, list) else [data]:
        for key in ("LicenseFile", "CopyrightFile"):
            if item.get(key):
                out.add(os.path.relpath(os.path.normpath(os.path.join(dirpath, item[key])), base))
        for f in item.get("LicenseFiles") or []:
            out.add(os.path.relpath(os.path.normpath(os.path.join(dirpath, f)), base))
print("\n".join("/" + p for p in sorted(out)))
PY
}

for repo in qtbase qtsvg qtimageformats qtmultimedia; do
    d=$DIR/$repo
    sparse "$d" "https://github.com/qt/$repo" "v$VERSION" '**/qt_attribution.json' '/LICENSES/'
    mapfile -t extra < <(named "$d")
    sparse "$d" "https://github.com/qt/$repo" "v$VERSION" '**/qt_attribution.json' '/LICENSES/' "${extra[@]}"
done

# Qt PDF: the chromium checkout qtwebengine's tag points to
# (GitHub's copy of qtwebengine has no tag v6.12.0 (2026-11-11, only the betas): then its branch 6.12, whose
# license files are those of the release)
W=$DIR/qtwebengine
WREF=v$VERSION
git ls-remote --exit-code https://github.com/qt/qtwebengine "refs/tags/$WREF" >/dev/null 2>&1 || WREF=${VERSION%.*}
if [ ! -d "$W/.git" ]; then
    git clone -q --filter=blob:none --no-checkout --depth 1 --branch "$WREF" https://github.com/qt/qtwebengine "$W"
fi
CHROMIUM=$(git -C "$W" ls-tree HEAD src/3rdparty | awk '{print $3}')
echo "qtwebengine $WREF ($(git -C "$W" rev-parse --short HEAD)) builds qtwebengine-chromium $CHROMIUM"
echo "$WREF $(git -C "$W" rev-parse HEAD)" > "$W/REF"
C=$DIR/chromium
if [ ! -d "$C/.git" ]; then
    git init -q "$C"
    git -C "$C" remote add origin https://github.com/qt/qtwebengine-chromium
fi
git -C "$C" fetch -q --filter=blob:none --depth 1 origin "$CHROMIUM"
git -C "$C" sparse-checkout set --no-cone /chromium/LICENSE /chromium/third_party/pdfium/LICENSE \
    /chromium/third_party/abseil-cpp/LICENSE /chromium/third_party/freetype/src/docs/FTL.TXT \
    /chromium/third_party/icu/LICENSE /chromium/third_party/libjpeg_turbo/LICENSE.md \
    /chromium/third_party/libjpeg_turbo/README.ijg /chromium/third_party/libpng/LICENSE \
    /chromium/third_party/zlib/LICENSE /chromium/third_party/pdfium/third_party/libopenjpeg/opj_includes.h \
    /chromium/third_party/pdfium/third_party/lcms/src/cmscnvrt.c /chromium/third_party/pdfium/third_party/agg23/agg_basics.h \
    /chromium/third_party/pdfium/third_party/bigint/LICENSE /chromium/third_party/pdfium/third_party/fp16/LICENSE
git -C "$C" checkout -q FETCH_HEAD
echo "$CHROMIUM" > "$C/COMMIT"

# ICU as Qt ships it
sparse "$DIR/icu" https://github.com/unicode-org/icu "$ICU_TAG" /LICENSE /icu4c/LICENSE
echo "Qt $VERSION license files: $DIR"
