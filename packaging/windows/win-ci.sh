#!/usr/bin/env bash
# Windows build on GitHub Actions (.github/workflows/windows.yml, port steps W1 and W2, 2026-11-07).
# W2: the writing tools, importers and Media Playback as Windows add-ons (packaging/windows/win-addons.sh), a
# program update package, the symbols file (.pdb), and the checks of packaging/windows/win-check.sh.
# W4a (2026-11-11): Qt 6.12.0, runtime win64-qt6.12-msvc-r1. W4c (2026-11-12): whisper.cpp (packaging/windows/win-whisper.sh)
# and the add-on "Speech Transcription".
# Runs in Git Bash on the runner after Qt, MSVC's environment, vcpkg and sccache are set up.
# Never stops at the first failure: every step writes its result into results/, which the
# workflow puts on the pre-release "win-ci", where a Claude session can read it (the session
# cannot read the Actions API).
#
#   packaging/windows/win-ci.sh <results dir>
set -u
OUT=${1:-results}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
SUM="$OUT/summary.txt"
: > "$SUM"
say() { echo "$*" | tee -a "$SUM"; }
t0=$(date +%s)
mins() { echo "$(( ($(date +%s) - t0) / 60 )) min"; }

# 0.91.1 (2026-11-14, the first public Windows files): QB_WIN_STAGE=release builds what a public release needs and
# stops before the checks and tests, which need the test switches: the program without test switches (so it
# refuses unsigned packages, as a released Linux program does), the program update, the add-ons and the update
# bundle, all unsigned yet, in <results>/stage. packaging/windows/win-release.sh goes on once they are signed
# (.github/workflows/release-windows.yml). Without the variable: the test build of the private repository, as before.
STAGE=${QB_WIN_STAGE:-test}
if [ "$STAGE" = release ]; then
  SWITCHES="-DQB_BUILD_TESTS=OFF -DQB_TEST_SWITCHES=OFF"
else
  SWITCHES="-DQB_BUILD_TESTS=ON -DQB_TEST_SWITCHES=ON"
fi

ver=$(sed -n 's/^set(APP_VERSION "\(.*\)")/\1/p' CMakeLists.txt)
say "commit: ${GITHUB_SHA:-local}"
say "version: $ver"
say "qt: ${QT_ROOT_DIR:-unknown}"
say "msvc: ${VCToolsVersion:-unknown}"
say "triplet: ${VCPKG_TRIPLET:-unknown}"

QT=$(cygpath -m "$QT_ROOT_DIR")
VCPKG=$(cygpath -m "$VCPKG_INSTALLATION_ROOT")

# ---- whisper.cpp for the add-on "Speech Transcription" (W4c, 2026-11-12; processor only; cached by the workflow)
WHISPER="$PWD/.whisper"
bash packaging/windows/win-whisper.sh "$WHISPER" > "$OUT/whisper.log" 2>&1
rc=$?
say "whisper.cpp: rc=$rc, $(cat "$WHISPER/version.txt" 2>/dev/null | cut -d' ' -f1), modules: $(ls "$WHISPER/bin" 2>/dev/null | grep -i '^ggml-cpu-' | sed 's/^ggml-cpu-//; s/\.dll$//' | tr '\n' ' ')($(mins))"
[ $rc -eq 0 ] && export QB_WHISPER_DIR="$(cygpath -m "$WHISPER")"

# ---- configure
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe \
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache \
  -DCMAKE_PREFIX_PATH="$QT" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
  $SWITCHES -DQB_BUILD_TOOLS=ON -DQB_BUILD_TOOL_PLUGINS=ON \
  -DQB_BUILD_IMPORTER_PLUGINS=ON -DQB_BUILD_MEDIA_PLUGIN=ON -DQB_BUILD_EXAMPLE_PLUGIN=ON \
  > "$OUT/configure.log" 2>&1
rc=$?
say "configure: rc=$rc, stage $STAGE ($(mins))"
grep -iE 'hunspell|error|warning' "$OUT/configure.log" | head -20 >> "$SUM"
if [ $rc -ne 0 ]; then
  tail -40 "$OUT/configure.log" > "$OUT/errors.txt"
  say "result: configure failed"
  exit 0
fi

# ---- build (-k 0: go on after errors, so one run shows all of them)
cmake --build build -- -k 0 > "$OUT/build.log" 2>&1
rc=$?
grep -E ': (fatal )?error [A-Z]+[0-9]+|error LNK|: error:|FAILED:' "$OUT/build.log" | sort -u > "$OUT/errors.txt"
say "build: rc=$rc, error lines $(grep -c 'error' "$OUT/errors.txt"), failed files $(grep -c '^FAILED:' "$OUT/errors.txt") ($(mins))"
grep -c ': warning C' "$OUT/build.log" | sed 's/^/compiler warnings: /' >> "$SUM"
sccache --show-stats > "$OUT/sccache.txt" 2>&1
if [ $rc -ne 0 ] || [ ! -f build/quillbinder.exe ]; then
  say "result: build failed"
  exit 0
fi

# ---- the program with Qt's libraries and the C++ runtime (a folder to unzip anywhere)
DIST="dist/Quillbinder"
rm -rf dist && mkdir -p "$DIST"
cp build/quillbinder.exe "$DIST/"
"$QT/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw \
  "$DIST/quillbinder.exe" > "$OUT/deploy.log" 2>&1
say "deploy: rc=$? ($(mins))"
# W4a: the platform without a window, which windeployqt of Qt 6.12 leaves out: the program's own self-test (run before
# a program update is put in place, and by the checks below) starts with it; without it Qt stops with a message box
# that nobody closes (run 5 of W4a: every start timed out)
mkdir -p "$DIST/platforms" && cp "$QT/plugins/platforms/qoffscreen.dll" "$DIST/platforms/"
say "platform plugins: $(ls "$DIST/platforms" | tr '\n' ' ')"
crt=$(cygpath -u "${VCToolsRedistDir:-}" 2>/dev/null)
for d in "$crt"/x64/Microsoft.VC*.CRT; do
  [ -d "$d" ] && cp "$d"/*.dll "$DIST/"
done
cp LICENSE "$DIST/LICENSE.txt"
# W3: the exporter plugins that the AppImage carries in usr/lib/quillbinder/plugins (Markdown)
mkdir -p "$DIST/plugins" && cp build/plugins/*.dll "$DIST/plugins/" 2>/dev/null
# W3: what a Widgets program does not need (the C++ runtime DLLs are beside the program already;
# the DirectX shader compiler is for Direct3D 12 shaders, which Qt Widgets does not compile)
rm -f "$DIST/vc_redist.x64.exe" "$DIST/dxcompiler.dll" "$DIST/dxil.dll"
# W4a (Qt 6.12, Qt Image Formats installed for WebP): the image plugins the AppImage carries (gif, ico, jpeg, pdf,
# svg, webp); TIFF, ICNS, TGA and WBMP stay out, as on Linux (and out of the license list)
rm -f "$DIST"/imageformats/q{tiff,icns,tga,wbmp}.dll
say "image plugins: $(ls "$DIST/imageformats" 2>/dev/null | tr '\n' ' ')"
say "styles: $(ls "$DIST/styles" 2>/dev/null | tr '\n' ' ')"
say "icu or other unicode dlls: $(ls "$DIST" | grep -iE '^icu' | tr '\n' ' ')"
# W3: spelling dictionaries (English and German, LibreOffice's), checked against their SHA-256 sums
bash packaging/windows/win-dictionaries.sh "$DIST" > "$OUT/dictionaries.log" 2>&1
say "dictionaries: rc=$? $(ls "$DIST/dictionaries" 2>/dev/null | grep -c '\.dic$') languages"
ls "$DIST" > "$OUT/files.txt"
say "files: $(find "$DIST" -type f | wc -l), $(du -sm "$DIST" | cut -f1) MiB"

# ---- self-test: starts invisibly (offscreen), builds the window, checks the page view, ends
( cd "$DIST" && QT_QPA_PLATFORM=offscreen timeout 120 ./quillbinder.exe --selftest > "$OUT/selftest.txt" 2>&1 )
rc=$?
say "selftest: rc=$rc, output: $(tr -d '\r' < "$OUT/selftest.txt" | grep -m1 'selftest' || echo none)"

selftest_ok=0
[ $rc -eq 0 ] && grep -q 'selftest ok' "$OUT/selftest.txt" && selftest_ok=1
# W4b: the program is portable: its start made the folder "data" beside it (settings ...); the downloads carry none
rm -rf "$DIST/data"

ZIP="$OUT/Quillbinder-$ver-windows-x64.zip"
( cd dist && 7z a -tzip -mx=7 "$ZIP" Quillbinder > /dev/null )
say "zip: $(du -k "$ZIP" | cut -f1) KiB"

# ---- the symbols file (.pdb): turns the offsets of crash reports into function names (W2)
if [ -f build/quillbinder.pdb ]; then
  ( cd build && 7z a -tzip -mx=7 "$OUT/Quillbinder-$ver-windows-x64-symbols.zip" quillbinder.pdb > /dev/null )
  say "symbols: $(du -k "$OUT/Quillbinder-$ver-windows-x64-symbols.zip" | cut -f1) KiB"
else
  say "symbols: no quillbinder.pdb"
fi

# ---- a program update package of this build (W2)
PY=$(command -v python3 || command -v python)
runtime=$(sed -n 's/^  set(APP_RUNTIME "\(.*\)")/\1/p' CMakeLists.txt)
"$PY" tools/make-package.py core --binary "$(cygpath -w "$PWD/build/quillbinder.exe")" --version "$ver" \
  --runtime "$runtime" --name "Quillbinder program update (Windows)" --description "Program update to version $ver." \
  "$(cygpath -w "$OUT/core.qbpkg")" > /dev/null 2>&1
say "program update: runtime $runtime, $(du -k "$OUT/core.qbpkg" 2>/dev/null | cut -f1) KiB"

# ---- what Qt brings for audio and video (decides the media add-on's backends)
say "qt multimedia plugins: $(ls "$QT/plugins/multimedia" 2>/dev/null | tr '\n' ' ')"
say "qt ffmpeg libraries: $(ls "$QT/bin" 2>/dev/null | grep -iE '^(av|sw)[a-z]*-?[0-9]*\.dll$' | tr '\n' ' ')"
say "plugin dlls: $(ls build/tool-plugins build/tool-plugins/media build/tool-plugins/speech 2>/dev/null | grep -i '\.dll$' | tr '\n' ' ')"

# ---- add-ons for Windows (W2)
mkdir -p "$OUT/addons"
bash packaging/windows/win-addons.sh build "$QT" "$OUT/addons" > "$OUT/addons.log" 2>&1
say "addons: rc=$? $(ls "$OUT/addons" | grep -c '\.qbpkg$') packages ($(mins))"

# the program update and every add-on in one bundle (needs $singles)
make_bundle() {
  "$PY" tools/make-package.py bundle --id quillbinder-update --name "Quillbinder $ver for Windows, with all add-ons" \
    --version "$ver" --description "The program update to $ver and every add-on in one package. Parts that are already there in the same version are left out." \
    --whats-new "Program update and all add-ons" \
    $(for p in "$OUT/core.qbpkg" $singles; do cygpath -w "$p"; done) "$(cygpath -w "$OUT/quillbinder-$ver-windows-update.qbpkg")" > /dev/null
}

# ---- the release stage ends here: everything the signing job and packaging/windows/win-release.sh need, in <results>/stage
if [ "$STAGE" = release ]; then
  singles=$(ls "$OUT"/addons/*.qbpkg | grep -v 'media-all-formats')
  make_bundle
  mkdir -p "$OUT/stage/packages"
  cp "$OUT/core.qbpkg" "$OUT"/addons/*.qbpkg "$OUT/quillbinder-$ver-windows-update.qbpkg" "$OUT/stage/packages/"
  ( cd dist && 7z a -tzip -mx=3 "$OUT/stage/program-folder.zip" Quillbinder > /dev/null )
  cp "$OUT/Quillbinder-$ver-windows-x64-symbols.zip" "$OUT/stage/" 2>/dev/null
  echo "$ver" > "$OUT/stage/version.txt"
  npk=$(ls "$OUT/stage/packages" | grep -c '\.qbpkg$')
  say "stage: $npk packages, program folder $(du -k "$OUT/stage/program-folder.zip" | cut -f1) KiB"
  if [ $selftest_ok -eq 1 ] && [ "$npk" -ge 12 ] && [ -f "$OUT/stage/Quillbinder-$ver-windows-x64-symbols.zip" ]; then
    say "result: ok"
  else
    say "result: release build incomplete (selftest $selftest_ok, packages $npk)"
  fi
  say "time: $(mins)"
  exit 0
fi

# ---- checks of W2: update loader, crash and freeze reports, sessions, add-ons, instance link
# (W4b: on a copy, since the portable program keeps its data beside itself)
rm -rf check-program && mkdir -p check-program && cp -r "$DIST" check-program/
bash packaging/windows/win-check.sh check-program/Quillbinder "$OUT/addons" "$OUT" > "$OUT/checks-run.log" 2>&1
crc=$?
rm -rf check-program
grep -E '^check ' "$OUT/checks-run.log" | grep -v ': ok$' >> "$SUM"
say "$(grep -E '^checks: ' "$OUT/checks-run.log" || echo 'checks: did not finish')"
# ---- the three downloads (W3), as on Linux:
#  main:   Quillbinder-<v>-windows-x64.zip (made above: the program, Qt, dictionaries; no add-ons)
#  update: quillbinder-<v>-windows-update.qbpkg, the program update and every add-on in one bundle
#  full:   Quillbinder-<v>-full-windows-x64.zip, the program with every add-on in full-addons\, which
#          it installs at its first start
singles=$(ls "$OUT"/addons/*.qbpkg | grep -v 'media-all-formats')
make_bundle
say "update bundle: $(echo $singles | wc -w) add-ons, $(du -k "$OUT/quillbinder-$ver-windows-update.qbpkg" 2>/dev/null | cut -f1) KiB"
rm -rf full && mkdir -p full && cp -r "$DIST" full/ && mkdir -p full/Quillbinder/full-addons && cp $singles full/Quillbinder/full-addons/
( cd full && 7z a -tzip -mx=7 "$OUT/Quillbinder-$ver-full-windows-x64.zip" Quillbinder > /dev/null )
say "full: $(du -k "$OUT/Quillbinder-$ver-full-windows-x64.zip" | cut -f1) KiB"

# ---- the setup programs (W4d, 0.90.4): the main and the full folder as a setup .exe each, made with Inno Setup. A
# setup only unpacks into a folder the writer picks: no admin rights, no uninstaller, no registry entry (the checks of
# packaging/windows/win-setup.sh)
bash packaging/windows/win-setup.sh "$ver" "$DIST" full/Quillbinder "$OUT" > "$OUT/setup-run.log" 2>&1
src=$?
grep -E '^setup: ' "$OUT/setup-run.log" >> "$SUM"
grep -E '^check ' "$OUT/setup-run.log" | grep -v ': ok$' >> "$SUM"
say "$(grep -E '^checks: ' "$OUT/setup-run.log" | sed 's/^checks:/setup checks:/' || echo 'setup checks: did not finish')"
rm -rf check-main && mkdir -p check-main && cp -r "$DIST" check-main/
bash packaging/windows/win-check-layout.sh check-main/Quillbinder full/Quillbinder "$OUT/quillbinder-$ver-windows-update.qbpkg" "$OUT" > "$OUT/layout-run.log" 2>&1
lrc=$?
rm -rf check-main
grep -E '^check ' "$OUT/layout-run.log" | grep -v ': ok$' >> "$SUM"
say "$(grep -E '^checks: ' "$OUT/layout-run.log" | sed 's/^checks:/layout checks:/' || echo 'layout checks: did not finish')"

# ---- tests on the runner (W3): suites built with MSVC, Qt's Windows platform
bash packaging/windows/win-tests.sh build "$QT" "$DIST" "$OUT" > "$OUT/tests-run.log" 2>&1
trc=$?
grep -E ' rc=| FAIL' "$OUT/tests-run.log" >> "$SUM"
say "$(grep -E '^tests: ' "$OUT/tests-run.log" || echo 'tests: did not finish')"

# the add-ons go up beside the other results
mv "$OUT"/addons/*.qbpkg "$OUT/" 2>/dev/null
rm -rf "$OUT/addons" "$OUT/c-update" "$OUT/c-linux-addon" "$OUT/core.qbpkg" full

if [ $selftest_ok -eq 1 ] && [ $crc -eq 0 ] && [ $lrc -eq 0 ] && [ $src -eq 0 ] && [ $trc -eq 0 ]; then
  say "result: ok"
elif [ $selftest_ok -eq 1 ]; then
  say "result: built, checks failed (program $crc, layout $lrc, setup $src, tests $trc)"
else
  say "result: built, selftest failed"
fi
say "time: $(mins)"
exit 0
