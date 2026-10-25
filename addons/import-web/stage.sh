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
