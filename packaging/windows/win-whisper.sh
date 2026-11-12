#!/usr/bin/env bash
# Builds whisper.cpp (speech recognition, MIT license) with MSVC for the Windows add-on "Speech Transcription"
# (port step W4c, 2026-11-12). Runs in Git Bash inside MSVC's environment (packaging/windows/win-ci.sh); the same
# release as on Linux (tools/get-whisper.sh).
#
#   packaging/windows/win-whisper.sh <prefix>
#
# The processor only (the writer's choice for the first Windows version, 2026-11-12): shared libraries, the compute
# backends as separate modules (GGML_BACKEND_DL), one CPU module per instruction set that MSVC can build (x64, sse42,
# sandybridge, haswell, skylakex, cannonlake, cascadelake, icelake, alderlake; ggml picks the best one for the
# writer's processor when it starts). No Vulkan, no OpenMP (its runtime DLL would have to come along).
# Result: <prefix>/include (whisper.h, ggml*.h), <prefix>/lib (import libraries), <prefix>/bin (whisper.dll,
# ggml.dll, ggml-base.dll, ggml-cpu-*.dll), <prefix>/samples/jfk.wav, LICENSE-whisper.cpp.txt, version.txt.
# A finished build of the same version and the same script is reused (the workflow caches <prefix>).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX=$1
TAG=v1.9.5
COMMIT=d1be6fde11ac6e0407606b4e42fe72d34add8037
URL=https://github.com/ggml-org/whisper.cpp
STAMP="$TAG $COMMIT $(sha256sum "$0" | cut -c1-16)"
if [ "$(cat "$PREFIX/version.txt" 2>/dev/null)" = "$STAMP" ] && [ -f "$PREFIX/bin/whisper.dll" ]; then
    echo "whisper.cpp is there: $PREFIX ($TAG)"
    exit 0
fi
WORK=$(mktemp -d)
git clone -q --depth 1 --branch "$TAG" "$URL" "$WORK/whisper.cpp"
test "$(git -C "$WORK/whisper.cpp" rev-parse HEAD)" = "$COMMIT"
rm -rf "$PREFIX"
launcher=()
command -v sccache > /dev/null && launcher=(-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache)
cmake -S "$(cygpath -m "$WORK/whisper.cpp")" -B "$(cygpath -m "$WORK/build")" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe "${launcher[@]}" \
    -DCMAKE_INSTALL_PREFIX="$(cygpath -m "$PREFIX")" \
    -DBUILD_SHARED_LIBS=ON -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON -DGGML_NATIVE=OFF \
    -DGGML_VULKAN=OFF -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DWHISPER_SDL2=OFF
cmake --build "$(cygpath -m "$WORK/build")"
cmake --install "$(cygpath -m "$WORK/build")"
# every DLL of the build in bin/ (the compute modules are not always installed there)
mkdir -p "$PREFIX/bin"
find "$WORK/build" -name '*.dll' -exec cp -n {} "$PREFIX/bin/" \;
cp "$WORK/whisper.cpp/LICENSE" "$PREFIX/LICENSE-whisper.cpp.txt"
# the add-on's test recording: J. F. Kennedy, inaugural address 1961 (public domain), whisper.cpp's own sample
mkdir -p "$PREFIX/samples"
cp "$WORK/whisper.cpp/samples/jfk.wav" "$PREFIX/samples/"
for f in whisper.dll ggml.dll ggml-base.dll ggml-cpu-x64.dll ggml-cpu-haswell.dll; do
    test -f "$PREFIX/bin/$f" || { echo "missing after the build: bin/$f" >&2; exit 1; }
done
for f in whisper.lib ggml.lib ggml-base.lib; do
    test -f "$PREFIX/lib/$f" || { echo "missing after the build: lib/$f" >&2; exit 1; }
done
echo "$STAMP" > "$PREFIX/version.txt"
rm -rf "$WORK"
echo "whisper.cpp built: $PREFIX ($TAG, processor only)"
ls "$PREFIX/bin"
