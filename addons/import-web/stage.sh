#!/bin/bash
# Puts the files of the add-on "Web Page Import" into a staged copy of the add-on.
#   stage.sh <build dir> <stage dir>      (called by packaging/build-addons.sh)
# The plugin of the program (build/tool-plugins/libqb-import-web.so) and Qt's TLS plugin of
# Ubuntu 24.04 (package libqt6network6t64, the same Qt as the AppImage, which has no TLS plugin).
set -euo pipefail
BUILD=$1
STAGE=$2
SYS=/usr/lib/x86_64-linux-gnu
P=$STAGE/payload
mkdir -p "$P/plugins" "$P/qtplugins/tls"
cp "$BUILD/tool-plugins/libqb-import-web.so" "$P/plugins/"
cp "$SYS/qt6/plugins/tls/libqopensslbackend.so" "$P/qtplugins/tls/"
strip --strip-unneeded "$P"/plugins/*.so "$P"/qtplugins/tls/*.so 2>/dev/null || true

# the license note, with the source of exactly this plugin (0.50)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC_URL="$(sed -n 's/^set(APP_HOMEPAGE *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")/releases/tag/third-party-sources"
read -r QT_PKG QT_VER QT_FILE < <(python3 -I "$ROOT/tools/collect-sources.py" --name ubuntu "$SYS/qt6/plugins/tls/libqopensslbackend.so")
VERSION=$(python3 -I -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$STAGE/manifest.json")
mkdir -p "$P/licenses"
cat > "$P/licenses/README.txt" <<TEXT
Files in this add-on ("Web Page Import" $VERSION)
============================================

plugins/libqb-import-web.so is part of Quillbinder (fetching, the article finder, the window).

qtplugins/tls/libqopensslbackend.so
    Qt's TLS plugin for OpenSSL (Qt Network 6.4.2), as built by Ubuntu 24.04 (source package
    $QT_PKG $QT_VER). It lets the program fetch secure (https) pages; the AppImage does not
    carry it. It uses the system's OpenSSL 3 (libssl.so.3, libcrypto.so.3).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: file $QT_FILE of the release "Third-party source code" of the project page:
    $SRC_URL
    (also from Ubuntu 24.04: apt-get source $QT_PKG=$QT_VER). It can be replaced by another
    build of the same Qt version.
TEXT
