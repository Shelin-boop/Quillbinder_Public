#!/bin/bash
# Builds a program update package (.qbpkg, kind "core") from the current
# source. Must run on the same system the AppImage was built on (same
# libraries); the runtime id in CMakeLists.txt (APP_RUNTIME) says which
# AppImages can use the result.
#
#   packaging/build-update.sh "What is new, line 1" "line 2" ...
#
# Result: dist/<id>-<version>-update.qbpkg
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$ROOT/build-update}
OUT=${OUT:-$ROOT/dist}
APP_ID=$(sed -n 's/^set(APP_ID *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
APP_NAME=$(sed -n 's/^set(APP_NAME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
VERSION=$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
RUNTIME=$(sed -n 's/^set(APP_RUNTIME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DQB_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null
STAGE=$BUILD/stage
rm -rf "$STAGE"; mkdir -p "$STAGE/plugins" "$OUT"
cp "$BUILD/$APP_ID" "$STAGE/$APP_ID"
# the program file before strip (same build ID): turns the addresses of a crash or
# freeze report into function names (developer guide, "Reading a crash report")
xz -9 -c "$BUILD/$APP_ID" > "$OUT/$APP_ID-$VERSION-symbols.xz"
strip "$STAGE/$APP_ID"
# exporter plugins that belong to this version
cp "$BUILD"/plugins/*.so "$BUILD"/plugins/*.json "$STAGE/plugins/" 2>/dev/null || true
strip "$STAGE"/plugins/*.so 2>/dev/null || true

ARGS=()
for line in "$@"; do ARGS+=(--whats-new "$line"); done
python3 "$ROOT/tools/make-package.py" core --binary "$STAGE/$APP_ID" --plugins "$STAGE/plugins" \
    --version "$VERSION" --runtime "$RUNTIME" --name "$APP_NAME program update" \
    --description "Program update to version $VERSION." "${ARGS[@]}" \
    "$OUT/$APP_ID-$VERSION-update.qbpkg"
