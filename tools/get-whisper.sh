#!/bin/bash
# Fetches whisper.cpp (speech recognition, MIT license) at a fixed release and builds it for the
# add-on "Speech Transcription" (0.61):
#   tools/get-whisper.sh [prefix]          (default build/whisper, or QB_WHISPER_DIR)
# Builds shared libraries with the compute backends as separate modules (GGML_BACKEND_DL): one CPU
# module per instruction set (the best one for the writer's processor is chosen when it starts, so
# the build machine's processor does not matter) and the Vulkan module for graphics cards (loaded
# only when a card is chosen; a system without Vulkan simply does not offer one).
# Needs git, cmake, ninja, libvulkan-dev, glslc and spirv-headers (tools/cloud-setup.sh installs them).
# Takes about 15 to 30 minutes on two cores (the Vulkan shaders take most of it). Run it once;
# a finished build with the same version is reused.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=${1:-${QB_WHISPER_DIR:-$ROOT/build/whisper}}
TAG=v1.9.5
COMMIT=d1be6fde11ac6e0407606b4e42fe72d34add8037
URL=https://github.com/ggml-org/whisper.cpp
WORK=$ROOT/build/whisper-src
if [ "$(cat "$PREFIX/version.txt" 2>/dev/null)" = "$TAG $COMMIT" ] && [ -f "$PREFIX/lib/libwhisper.so.1" ]; then
    echo "whisper.cpp is there: $PREFIX ($TAG)"
    exit 0
fi
if [ "$(git -C "$WORK/whisper.cpp" rev-parse HEAD 2>/dev/null)" != "$COMMIT" ]; then
    rm -rf "$WORK"
    mkdir -p "$WORK"
    git clone -q --depth 1 --branch "$TAG" "$URL" "$WORK/whisper.cpp"
fi
test "$(git -C "$WORK/whisper.cpp" rev-parse HEAD)" = "$COMMIT"
VULKAN=ON
if ! command -v glslc >/dev/null || ! [ -f /usr/include/vulkan/vulkan.h ] || ! [ -d /usr/share/cmake/SPIRV-Headers ]; then
    echo "glslc, the Vulkan or the SPIR-V headers are missing (apt-get install libvulkan-dev glslc spirv-headers): building without graphics cards" >&2
    VULKAN=OFF
fi
rm -rf "$PREFIX" "$WORK/build"
cmake -S "$WORK/whisper.cpp" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON -DGGML_NATIVE=OFF \
    -DGGML_VULKAN=$VULKAN -DGGML_OPENMP=OFF -DGGML_CCACHE=OFF \
    -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DWHISPER_SDL2=OFF \
    -DCMAKE_INSTALL_RPATH='$ORIGIN' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON >/dev/null
cmake --build "$WORK/build" -j"$(nproc)"
cmake --install "$WORK/build" >/dev/null
# the compute modules sit next to the libraries; ggml finds them by the folder it is given
find "$WORK/build" -name 'libggml-*.so' -newer "$WORK/build/CMakeCache.txt" -exec cp -n {} "$PREFIX/lib/" \;
cp "$WORK/whisper.cpp/LICENSE" "$PREFIX/LICENSE-whisper.cpp.txt"
# the add-on's test recording: J. F. Kennedy, inaugural address 1961 (public domain), whisper.cpp's own sample
mkdir -p "$PREFIX/samples"
cp "$WORK/whisper.cpp/samples/jfk.wav" "$PREFIX/samples/"
echo "$TAG $COMMIT" > "$PREFIX/version.txt"
echo "$VULKAN" > "$PREFIX/vulkan.txt"
echo "whisper.cpp built: $PREFIX ($TAG, Vulkan $VULKAN)"
ls "$PREFIX/lib"
