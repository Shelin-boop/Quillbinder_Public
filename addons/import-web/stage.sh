#!/bin/bash
# Puts the files of the add-on "Web Page Import" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/linux/build-addons.sh)
# The plugin of the program (build/tool-plugins/libqb-import-web.so) and Qt's TLS plugin of The Qt Company's own
# build of the Qt the AppImage carries (0.80.0: Qt 6.12.0 in QB_QT_DIR; the AppImage has no TLS plugin).
set -euo pipefail
BUILD=$1
STAGE=$2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
QT_VERSION=$(sed -n 's/^set(QB_QT_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
QT=${QB_QT_DIR:-/opt/qt/$QT_VERSION/gcc_64}
P=$STAGE/payload
mkdir -p "$P/plugins" "$P/qtplugins/tls"
cp "$BUILD/tool-plugins/libqb-import-web.so" "$P/plugins/"
cp "$QT/plugins/tls/libqopensslbackend.so" "$P/qtplugins/tls/"
strip --strip-unneeded "$P"/plugins/*.so "$P"/qtplugins/tls/*.so 2>/dev/null || true

# the license note, with the source of exactly this plugin (0.50)
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
QT_FILE=$(python3 -I "$ROOT/tools/collect-sources.py" --name qt)
VERSION=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$STAGE/manifest.json")
mkdir -p "$P/licenses"
cat > "$P/licenses/README.txt" <<TEXT
Files in this add-on ("Web Page Import" $VERSION)
============================================

plugins/libqb-import-web.so is part of Quillbinder (fetching, the article finder, the window).

qtplugins/tls/libqopensslbackend.so
    Qt's TLS plugin for OpenSSL (Qt Network $QT_VERSION), The Qt Company's own build for Linux,
    unchanged. It lets the program fetch secure (https) pages; the AppImage does not
    carry it. It uses the system's OpenSSL 3 (libssl.so.3, libcrypto.so.3).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: file $QT_FILE of the release "Third-party source code" of the project page:
    $SRC_URL
    (also https://download.qt.io/official_releases/qt/). It can be replaced by another
    build of the same Qt version.
TEXT
