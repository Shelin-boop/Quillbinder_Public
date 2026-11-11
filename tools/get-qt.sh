#!/bin/bash
# Qt for the Linux build (0.80.0): The Qt Company's own binaries of the version named in CMakeLists.txt
# (QB_QT_VERSION), unpacked into /opt/qt/<version>/gcc_64 (or QB_QT_DIR), where CMakeLists.txt and the packaging
# scripts look for it.
#
#   tools/get-qt.sh
#
# The cloud session cannot reach download.qt.io: the workflow .github/workflows/qt-linux.yml installs the same
# binaries with aqtinstall on GitHub's runner (push a branch qt/<version>) and puts them on the pre-release
# "qt-<version>-linux" of the private repository, from where this script downloads them (gh, logged in). Where
# download.qt.io can be reached (GitHub's runners, the release workflow of the public repository), the script
# installs the same files with aqtinstall instead.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^set(QB_QT_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
DEST=${QB_QT_DIR:-/opt/qt/$VERSION/gcc_64}
FILE=qt-$VERSION-linux-gcc_64.tar.xz
# the archive the workflow made for this version (2026-11-11, run 42959624084 of commit 657f2f7)
SHA256=bd112a287dc015b09b8d05486a1eebb18601135eecf57d80f16fdc9d795dace2
REPO=${QB_QT_REPO:-Shelin-boop/quillbinder}

if [ -f "$DEST/lib/libQt6Core.so.$VERSION" ] && [ -f "$DEST/bin/qmake" ]; then
    echo "Qt $VERSION is there: $DEST"
    exit 0
fi
# Where download.qt.io can be reached (GitHub's runners): aqtinstall, as the workflow qt-linux.yml does; aqtinstall
# checks every archive against the checksums of Qt's own repository
if curl -fsS -m 20 -o /dev/null https://download.qt.io/online/qtsdkrepository/ 2>/dev/null; then
    python3 -m pip install -q --break-system-packages 'aqtinstall==3.3.0' 2>/dev/null \
        || python3 -m pip install -q 'aqtinstall==3.3.0'
    BASE=$(dirname "$(dirname "$DEST")")
    python3 -m aqt install-qt linux desktop "$VERSION" linux_gcc_64 -O "$BASE" -m qtmultimedia qtimageformats qtpdf
    [ "$BASE/$VERSION/gcc_64" = "$DEST" ] || { rm -rf "$DEST"; mkdir -p "$(dirname "$DEST")"; mv "$BASE/$VERSION/gcc_64" "$DEST"; }
    rm -rf "$DEST/doc" "$DEST/examples"
    echo "Qt $VERSION installed with aqtinstall: $DEST"
    exit 0
fi
WORK=$ROOT/build/qt-download
mkdir -p "$WORK"
if ! echo "$SHA256  $WORK/$FILE" | sha256sum -c --status 2>/dev/null; then
    id=$(gh api "repos/$REPO/releases/tags/qt-$VERSION-linux" --jq ".assets[] | select(.name==\"$FILE\") | .id")
    [ -n "$id" ] || { echo "no $FILE on the pre-release qt-$VERSION-linux of $REPO (push a branch qt/$VERSION)" >&2; exit 1; }
    gh api "repos/$REPO/releases/assets/$id" -H 'Accept: application/octet-stream' > "$WORK/$FILE.part"
    mv "$WORK/$FILE.part" "$WORK/$FILE"
fi
echo "$SHA256  $WORK/$FILE" | sha256sum -c -
mkdir -p "$(dirname "$DEST")"
rm -rf "$DEST"
tar -C "$(dirname "$DEST")" -xJf "$WORK/$FILE"
[ -d "$(dirname "$DEST")/gcc_64" ] && [ "$(dirname "$DEST")/gcc_64" != "$DEST" ] && mv "$(dirname "$DEST")/gcc_64" "$DEST"
rm -f "$WORK/$FILE"
echo "Qt $VERSION unpacked: $DEST"
