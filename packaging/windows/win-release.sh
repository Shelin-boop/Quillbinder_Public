#!/usr/bin/env bash
# The public Windows files from a release build and its signed packages (0.91.1, 2026-11-14, the first public
# Windows files). Runs in Git Bash on GitHub's Windows machine (.github/workflows/release-windows.yml) after
# packaging/windows/win-ci.sh with QB_WIN_STAGE=release built the program and the job "sign-packages" signed the
# packages. It needs no compiler and no Qt: 7-Zip, Inno Setup (packaging/windows/win-setup.sh) and the program itself.
#
#   packaging/windows/win-release.sh <stage dir of win-ci.sh> <dir with the signed packages> <out dir>
#
# Makes in <out dir>/files, named as the release carries them (QB_WIN_NAME, "experimental" by the writer's choice of
# 2026-11-14; the version's label stands in its place when the variable is empty):
#   Quillbinder-<v>-<name>-windows-x64.zip, -setup.exe            the program alone
#   Quillbinder-<v>-full-<name>-windows-x64.zip, -setup.exe       with every add-on in full-addons\
#   quillbinder-<v>-<name>-windows-update.qbpkg                    the program update and every add-on (signed)
#   media-all-formats-windows.qbpkg                                the audio and video add-ons (signed), if built
#   Quillbinder-<v>-<name>-windows-x64-symbols.zip                 for crash reports
# and checks them with the program as released (no test switches): the setup programs (win-setup.sh), the signed
# bundle goes in without --allow-unsigned, an unsigned package is refused, the full version installs its add-ons at
# its first start. Lines "check <name>: ok|FAIL <why>" and "checks: ..." in <out dir>/summary.txt; exit status 0
# only when everything passed.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
STAGE=$(cd "$1" && pwd)
SIGNED=$(cd "$2" && pwd)
mkdir -p "$3/files"
OUT=$(cd "$3" && pwd)
FILES="$OUT/files"
SUM="$OUT/summary.txt"
: > "$SUM"
say() { echo "$*" | tee -a "$SUM"; }
ok=0
failed=0
pass() { say "check $1: ok"; ok=$((ok + 1)); }
fail() { say "check $1: FAIL $2"; failed=$((failed + 1)); }

ver=$(tr -d '\r\n' < "$STAGE/version.txt")
label=$(sed -n 's/^set(APP_VERSION_LABEL "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
name=${QB_WIN_NAME-experimental}
[ -n "$name" ] || name=$label
say "version: $ver, name: $name"

WORK="$PWD/release-work"
rm -rf "$WORK" && mkdir -p "$WORK/main" "$WORK/full" "$WORK/build-names"
( cd "$WORK/main" && 7z x -y "$STAGE/program-folder.zip" > /dev/null )
MAIN="$WORK/main/Quillbinder"
[ -f "$MAIN/quillbinder.exe" ] || { say "result: no program folder in $STAGE/program-folder.zip"; exit 1; }
[ ! -e "$MAIN/data" ] && [ ! -e "$MAIN/full-addons" ] && pass main-clean || fail main-clean "data or full-addons in the program folder"

# ---- the signed packages: every one of the build, none unsigned
bundle="$SIGNED/quillbinder-$ver-windows-update.qbpkg"
singles=$(ls "$SIGNED"/*.qbpkg | grep -v -e '/media-all-formats' -e '/core\.qbpkg$' -e '-windows-update\.qbpkg$')
nbuilt=$(ls "$STAGE/packages" | grep -c '\.qbpkg$')
nsigned=$(ls "$SIGNED" | grep -c '\.qbpkg$')
nfull=$(echo $singles | wc -w)
[ "$nbuilt" -eq "$nsigned" ] && [ -f "$bundle" ] && [ "$nfull" -ge 10 ] && pass signed-all \
    || fail signed-all "built $nbuilt, signed $nsigned, for the full version $nfull"

# ---- the full version: the program with every signed add-on beside it
cp -r "$MAIN" "$WORK/full/"
FULL="$WORK/full/Quillbinder"
mkdir -p "$FULL/full-addons" && cp $singles "$FULL/full-addons/"

# ---- zips and setup programs under the build's names (win-setup.sh makes and checks the setup programs)
( cd "$WORK/main" && 7z a -tzip -mx=7 "$WORK/build-names/Quillbinder-$ver-windows-x64.zip" Quillbinder > /dev/null )
( cd "$WORK/full" && 7z a -tzip -mx=7 "$WORK/build-names/Quillbinder-$ver-full-windows-x64.zip" Quillbinder > /dev/null )
bash "$ROOT/packaging/windows/win-setup.sh" "$ver" "$MAIN" "$FULL" "$WORK/build-names" > "$OUT/setup-run.log" 2>&1
src=$?
grep -E '^setup: ' "$OUT/setup-run.log" >> "$SUM"
grep -E '^check ' "$OUT/setup-run.log" | grep -v ': ok$' >> "$SUM"
say "$(grep -E '^checks: ' "$OUT/setup-run.log" | sed 's/^checks:/setup checks:/' || echo 'setup checks: did not finish')"
[ $src -eq 0 ] && pass setup-programs || fail setup-programs "win-setup.sh ended with $src (setup-run.log)"
rm -rf "$MAIN/data" "$FULL/data"

# ---- checks with the program as released (on copies: the portable program keeps its data beside itself)
run() { # <folder> <seconds> <output file> <args...>
    local dir=$1 secs=$2 out=$3
    shift 3
    ( cd "$dir" && QT_QPA_PLATFORM=offscreen timeout "$secs" ./quillbinder.exe "$@" > "$out" 2>&1 )
}
count_addons() { ls -d "$1/data/addons"/*/ 2>/dev/null | wc -l; }
C="$WORK/check"
rm -rf "$C" && mkdir -p "$C/main" "$C/full" && cp -r "$MAIN" "$C/main/" && cp -r "$FULL" "$C/full/"
CM="$C/main/Quillbinder"
CF="$C/full/Quillbinder"

run "$CM" 180 "$OUT/c-selftest.txt" --selftest
rc=$?
[ $rc -eq 0 ] && grep -q 'selftest ok' "$OUT/c-selftest.txt" && pass selftest \
    || fail selftest "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-selftest.txt" | head -c 300)"
run "$CM" 60 "$OUT/c-version.txt" --version
grep -q "$ver" "$OUT/c-version.txt" && pass version || fail version "$(tr '\r\n' '  ' < "$OUT/c-version.txt" | head -c 200)"

# an unsigned package (as the build made it) is refused: nothing is installed
rm -rf "$CM/data"
unsigned=$(ls "$STAGE/packages"/*.qbpkg | grep -v -e '/media-all-formats' -e '/core\.qbpkg$' -e '-windows-update\.qbpkg$' | head -1)
run "$CM" 180 "$OUT/c-unsigned.txt" --install "$(cygpath -w "$unsigned")"
rc=$?
[ $rc -ne 0 ] && [ "$(count_addons "$CM")" -eq 0 ] && pass unsigned-refused \
    || fail unsigned-refused "rc=$rc add-ons $(count_addons "$CM"); $(tr '\r\n' '  ' < "$OUT/c-unsigned.txt" | head -c 300)"

# the signed bundle goes in without --allow-unsigned: every add-on, and no program update of the same version
rm -rf "$CM/data"
run "$CM" 300 "$OUT/c-bundle.txt" --install "$(cygpath -w "$bundle")"
rc=$?
[ $rc -eq 0 ] && [ "$(count_addons "$CM")" -eq "$nfull" ] && pass signed-bundle \
    || fail signed-bundle "rc=$rc add-ons $(count_addons "$CM") of $nfull; $(tr '\r\n' '  ' < "$OUT/c-bundle.txt" | head -c 400)"
run "$CM" 180 "$OUT/c-selftest2.txt" --selftest
rc=$?
[ $rc -eq 0 ] && grep -q 'selftest ok' "$OUT/c-selftest2.txt" && pass selftest-with-addons \
    || fail selftest-with-addons "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-selftest2.txt" | head -c 300)"

# the full version: its first start installs every add-on (a released program has no switch that ends it, so it is
# stopped after a while; the add-ons are installed before the window is made)
rm -rf "$CF/data"
run "$CF" 90 "$OUT/c-full.txt" --no-restore
rc=$?
marker=$(ls "$CF/data"/full-addons-installed-* 2>/dev/null | wc -l)
[ "$(count_addons "$CF")" -eq "$nfull" ] && [ "$marker" -eq 1 ] && pass full-first-start \
    || fail full-first-start "rc=$rc add-ons $(count_addons "$CF") of $nfull, marker $marker; $(tr '\r\n' '  ' < "$OUT/c-full.txt" | head -c 300)"
taskkill //F //IM quillbinder.exe > /dev/null 2>&1
rm -rf "$C"

# ---- the files under the names the release carries
cp "$WORK/build-names/Quillbinder-$ver-windows-x64.zip" "$FILES/Quillbinder-$ver-$name-windows-x64.zip"
cp "$WORK/build-names/Quillbinder-$ver-full-windows-x64.zip" "$FILES/Quillbinder-$ver-full-$name-windows-x64.zip"
cp "$WORK/build-names/Quillbinder-$ver-windows-x64-setup.exe" "$FILES/Quillbinder-$ver-$name-windows-x64-setup.exe"
cp "$WORK/build-names/Quillbinder-$ver-full-windows-x64-setup.exe" "$FILES/Quillbinder-$ver-full-$name-windows-x64-setup.exe"
cp "$bundle" "$FILES/quillbinder-$ver-$name-windows-update.qbpkg"
cp "$STAGE/Quillbinder-$ver-windows-x64-symbols.zip" "$FILES/Quillbinder-$ver-$name-windows-x64-symbols.zip"
for p in "$SIGNED"/media-all-formats*.qbpkg; do
    [ -f "$p" ] || continue
    b=$(basename "$p")
    case "$b" in *windows*) cp "$p" "$FILES/$b" ;; *) cp "$p" "$FILES/${b%.qbpkg}-windows.qbpkg" ;; esac
done
n=$(ls "$FILES" | wc -l)
[ "$n" -ge 6 ] && [ -z "$(find "$FILES" -type f -size -100k)" ] && pass files || fail files "$n files; small: $(find "$FILES" -type f -size -100k | tr '\n' ' ')"
( cd "$FILES" && ls -l ) >> "$SUM"
say "checks: $ok ok, $failed failed"
if [ $failed -eq 0 ]; then say "result: ok"; else say "result: checks failed"; fi
[ $failed -eq 0 ]
