#!/usr/bin/env bash
# Checks of the three Windows downloads on the runner (port, W3, 2026-11-08): the main program, the
# update with all add-ons, the full version. Runs in Git Bash (packaging/windows/win-ci.sh) on copies of the program folders
# (W4b: the portable program keeps its data in "data" beside itself). Lines "check <name>: ok|FAIL <why>", then
# "checks: ...".
#
#   packaging/windows/win-check-layout.sh <main program folder> <full program folder> <update bundle> <results dir>
set -u
MAIN=$(cd "$1" && pwd)
FULL=$(cd "$2" && pwd)
UPDATE=$3
OUT=$(cd "$4" && pwd)
DATA="$MAIN/data"
ADDONS="$DATA/addons"
ok=0
failed=0
pass() { echo "check $1: ok"; ok=$((ok + 1)); }
fail() { echo "check $1: FAIL $2"; failed=$((failed + 1)); }
run() { # <folder> <seconds> <output file> <args...>
    local dir=$1 secs=$2 out=$3
    shift 3
    ( cd "$dir" && QT_QPA_PLATFORM=offscreen QB_NO_DIALOGS=1 timeout "$secs" ./quillbinder.exe "$@" > "$out" 2>&1 )
}
count_addons() { ls -d "$ADDONS"/*/ 2>/dev/null | wc -l; }
expected=$(ls "$FULL/full-addons"/*.qbpkg | wc -l)

# ---- main: dictionaries, nothing that is not needed, no add-ons
dics=$(ls "$MAIN/dictionaries"/*.dic 2>/dev/null | wc -l)
[ "$dics" -eq 5 ] && [ -f "$MAIN/dictionaries/licenses/README.txt" ] && pass main-dictionaries || fail main-dictionaries "$dics .dic files"
extra=$(ls "$MAIN" | grep -icE '^(vc_redist\.x64\.exe|dxcompiler\.dll|dxil\.dll)$')
[ "$extra" -eq 0 ] && pass main-no-extras || fail main-no-extras "$extra files left"
[ ! -d "$MAIN/full-addons" ] && pass main-no-addons || fail main-no-addons "full-addons in the main program"

# ---- the update with all add-ons, on the main program: the add-ons go in, the program update is left out
rm -rf "$DATA"
run "$MAIN" 300 "$OUT/c-layout-update.txt" --install "$(cygpath -w "$UPDATE")" --allow-unsigned
rc=$?
log=$(tr -d '\r' < "$OUT/c-layout-update.txt")
if [ $rc -eq 0 ] && grep -q 'already there' <<< "$log" && [ "$(count_addons)" -eq "$expected" ] && [ ! -d "$DATA/core" -o -z "$(ls "$DATA/core" 2>/dev/null | grep -v state.json)" ]; then
    pass update-bundle-on-main
else
    fail update-bundle-on-main "rc=$rc add-ons $(count_addons) of $expected; $(head -c 400 <<< "$log")"
fi

# ---- the full version: its first start installs every add-on once, a second start nothing
rm -rf "$DATA"
DATA="$FULL/data"
ADDONS="$DATA/addons"
rm -rf "$DATA"
QB_TEST_CRASH=quit run "$FULL" 300 "$OUT/c-layout-full1.txt" --no-restore
rc=$?
marker=$(ls "$DATA"/full-addons-installed-* 2>/dev/null | wc -l)
[ $rc -eq 0 ] && [ "$(count_addons)" -eq "$expected" ] && [ "$marker" -eq 1 ] && pass full-first-start \
    || fail full-first-start "rc=$rc add-ons $(count_addons) of $expected, marker $marker; $(tr -d '\r' < "$OUT/c-layout-full1.txt" | head -c 300)"
rm -rf "$ADDONS/themes-extra"
QB_TEST_CRASH=quit run "$FULL" 120 "$OUT/c-layout-full2.txt" --no-restore
[ ! -d "$ADDONS/themes-extra" ] && pass full-once || fail full-once "the add-ons were installed again"

# ---- with "Extra Video and Audio Formats" installed, playback starts with Qt's FFmpeg
QB_TEST_CRASH=media run "$FULL" 120 "$OUT/c-layout-media.txt" --no-restore
grep -q '^media: started.*FFmpeg' "$OUT/c-layout-media.txt" && pass media-ffmpeg \
    || fail media-ffmpeg "$(tr '\r\n' '  ' < "$OUT/c-layout-media.txt" | head -c 400)"
# switched off in the Add-on Manager: the decoders of Windows again
printf '{"disabled":["media-extra-formats"]}' > "$ADDONS/state.json"
QB_TEST_CRASH=media run "$FULL" 120 "$OUT/c-layout-media-off.txt" --no-restore
grep -q '^media: started.*Media Foundation' "$OUT/c-layout-media-off.txt" && pass media-extra-off \
    || fail media-extra-off "$(tr '\r\n' '  ' < "$OUT/c-layout-media-off.txt" | head -c 400)"

rm -rf "$DATA" "$MAIN/data"
echo "checks: $ok ok, $failed failed"
[ $failed -eq 0 ]
