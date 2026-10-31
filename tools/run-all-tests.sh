#!/bin/bash
# Runs every test suite of Quillbinder and prints one line per suite.
#   tools/run-all-tests.sh [output dir] [offscreen|x11]
# Needs a build in build/ (cmake -S . -B build && cmake --build build -j"$(nproc)").
# Logs: <output dir>/<suite>.<mode>.log, screenshots: <output dir>/shots-<suite>/.
# The add-on packages (the media add-ons need FFmpeg) are built first when an FFmpeg
# prefix is there: QB_FFMPEG_DIR and QB_FFMPEG_FULL_DIR (default build/ffmpeg and
# build/ffmpeg-full, see tools/get-ffmpeg.sh).
# A suite passes when its last line says "0 failure(s)" and rc=0.
# Only some suites (names as printed below: qb_tests, ui, m6 ... m14, fixes, v017 ...):
#   QB_SUITES="v040 m13"   run these
#   QB_SAMPLE=10           add whole suites drawn at random until about 10 % of the checks are covered
#                          (counts from tests/check-counts.txt); QB_SEED=<n> repeats a draw
# A full run in x11 mode (neither variable set) writes tests/check-counts.txt anew.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/test-results}
MODE=${2:-offscreen}
export QB_FFMPEG_DIR=${QB_FFMPEG_DIR:-$ROOT/build/ffmpeg}
export QB_FFMPEG_FULL_DIR=${QB_FFMPEG_FULL_DIR:-$ROOT/build/ffmpeg-full}   # (0.50: the add-on "Extra Video and Audio Formats")
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$ROOT" || exit 1
B=build
if [ ! -x $B/tests/qb_tests ]; then
    echo "no build in $ROOT/$B: cmake -S . -B build && cmake --build build -j\$(nproc)"
    exit 1
fi
# which suites run: all, or QB_SUITES plus a random sample (QB_SAMPLE percent of the checks)
bin_of() { case $1 in qb_tests) echo qb_tests;; ui) echo qb_ui_tests;; menu_audit) echo qb_menu_audit;; *) echo "qb_$1_tests";; esac; }
SELECTED=""
COUNTS=""
RAN=""
finish() {
    for s in $SELECTED; do
        [[ " $RAN " == *" $s "* ]] || echo "(not run: $s; not built, or no line for it at the end of this file)"
    done
    if [ -s "$COUNTS" ]; then
        { echo "# Checks per suite on X11, written by tools/run-all-tests.sh after a full X11 run."
          echo "# $(date +%F), APP_VERSION $(sed -n 's/.*set(APP_VERSION "\{0,1\}\([0-9.]*\).*/\1/p' CMakeLists.txt | head -1)"
          cat "$COUNTS"; } > tests/check-counts.txt
    fi
    [ -n "$COUNTS" ] && rm -f "$COUNTS"
}
trap finish EXIT
if [ -n "$QB_SUITES" ] || [ -n "$QB_SAMPLE" ]; then
    SELECTED="$QB_SUITES"
    if [ -n "$QB_SAMPLE" ]; then
        [ -f tests/check-counts.txt ] || { echo "no tests/check-counts.txt: run the full suite once"; SELECTED=""; exit 1; }
        QB_SEED=${QB_SEED:-$(( (RANDOM << 15) | RANDOM ))}
        avail=""
        while read -r n c; do
            case $n in ''|\#*) continue;; esac
            [ -x "$B/tests/$(bin_of "$n")" ] && avail="$avail $n:$c"
        done < tests/check-counts.txt
        mapfile -t drawn < <(python3 -I - "$QB_SAMPLE" "$QB_SEED" $avail <<'EOF'
import random, sys
pct, seed = float(sys.argv[1]), int(sys.argv[2])
sets = [(a.split(":")[0], int(a.split(":")[1])) for a in sys.argv[3:]]
total = sum(c for _, c in sets)
random.Random(seed).shuffle(sets)
drawn, got = [], 0
for name, count in sets:
    if got >= total * pct / 100:
        break
    drawn.append(name)
    got += count
print(" ".join(drawn))
print(f"{got} of {total} checks, {100 * got / max(total, 1):.1f} %")
EOF
)
        echo "sample: seed $QB_SEED, suites ${drawn[0]} (${drawn[1]})"
        SELECTED="$SELECTED ${drawn[0]}"
    fi
    echo "suites: $(echo $SELECTED | tr ' ' '\n' | awk '!seen[$0]++' | tr '\n' ' ')"
elif [ "$MODE" = x11 ]; then
    COUNTS=$(mktemp)
fi
if [ -d "$QB_FFMPEG_DIR/lib" ]; then
    # FFmpeg built after the build folder was configured (0.60): CMake keeps "headers not found", and the
    # player of Media Playback would lack its format check; configure and build again first
    if [ -d "$QB_FFMPEG_DIR/include" ] && grep -q '^QB_FFMPEG_INCLUDE:PATH=.*NOTFOUND' $B/CMakeCache.txt 2>/dev/null; then
        echo "(FFmpeg is newer than the build's configuration: configuring and building again)"
        { cmake -U QB_FFMPEG_INCLUDE -S . -B $B && cmake --build $B -j"$(nproc)"; } > "$OUT/reconfigure.log" 2>&1 \
            || { echo "configuring again failed"; tail "$OUT/reconfigure.log"; }
        rm -f $B/addons-dist/media-playback.qbpkg
    fi
    # (re)built when a package is missing or older than its add-on's files (or the template tool)
    stale=0
    for d in addons/*/; do
        id=$(basename "$d")
        pkg=$B/addons-dist/$id.qbpkg
        if [ ! -f "$pkg" ] || [ -n "$(find "$d" -newer "$pkg" -not -path '*/payload/templates/*' | head -1)" ] \
           || [ $B/qb_template -nt "$pkg" ]; then
            stale=1
        fi
    done
    if [ $stale = 1 ]; then
        packaging/build-addons.sh $B $B/addons-dist > "$OUT/addons.log" 2>&1 || { echo "add-ons failed"; tail "$OUT/addons.log"; }
        # the template files in the repository are made anew with new time stamps: keep the committed ones
        git checkout -- addons/*/payload/templates/ 2>/dev/null
    fi
else
    echo "(no FFmpeg in $QB_FFMPEG_DIR: add-ons not built, the media checks of m11 may fail)"
fi
# a sound server for the media tests (a null sink is enough)
if command -v pactl > /dev/null && ! pactl info > /dev/null 2>&1; then
    pulseaudio -D --exit-idle-time=-1 --system=false > /dev/null 2>&1 || true
    sleep 2
fi
PS=$(ls -d /tmp/pulse-*/native 2>/dev/null | head -1)
[ -n "$PS" ] && export PULSE_SERVER=unix:$PS
run() {
    local name=$1; shift
    [ -n "$SELECTED" ] && [[ " $SELECTED " != *" $name "* ]] && return
    RAN="$RAN $name"
    local log="$OUT/$name.$MODE.log"
    local start=$(date +%s)
    # every suite gets a fresh home, so settings and add-ons do not leak between suites
    local h=$(mktemp -d /tmp/qbhome.XXXX)
    if [ "$MODE" = x11 ]; then
        HOME=$h XDG_RUNTIME_DIR=$h timeout 1800 xvfb-run -a -s "-screen 0 1920x1080x24" "$@" > "$log" 2>&1
    else
        HOME=$h XDG_RUNTIME_DIR=$h QT_QPA_PLATFORM=offscreen timeout 1800 "$@" > "$log" 2>&1
    fi
    local rc=$?
    # checks of child runs carry a prefix: "[GStreamer] PASS ..."
    local pass=$(grep -c "^PASS\|\] PASS \|^  ok\|^ok \|\[ok\]" "$log")
    local fail=$(grep -ci "^FAIL\|\] FAIL \|\[fail\]\|^  FAIL" "$log")
    local summary=$(grep -E "^[0-9]+ failure\(s\)" "$log" | tail -1)
    echo "$name rc=$rc pass=$pass fail=$fail time=$(( $(date +%s) - start ))s ${summary:-(no summary line)}"
    [ -n "$COUNTS" ] && echo "$name $((pass + fail))" >> "$COUNTS"
    grep -i "^FAIL\|\] FAIL " "$log" | head -5 | sed 's/^/    /'
    rm -rf "$h"
}
run qb_tests $B/tests/qb_tests
run ui $B/tests/qb_ui_tests "$OUT/shots-ui"
for m in 6 7 8 9 10 11 12 13 14; do   # (m15, the tutorial of 0.15 to 0.30, is gone)
    [ -x $B/tests/qb_m${m}_tests ] && run m$m $B/tests/qb_m${m}_tests "$OUT/shots-m$m"
done
[ -x $B/tests/qb_fixes_tests ] && run fixes $B/tests/qb_fixes_tests "$OUT/shots-fixes"
[ -x $B/tests/qb_v017_tests ] && run v017 $B/tests/qb_v017_tests "$OUT/shots-v017"
[ -x $B/tests/qb_v018_tests ] && run v018 $B/tests/qb_v018_tests "$OUT/shots-v018"
[ -x $B/tests/qb_v019_tests ] && run v019 $B/tests/qb_v019_tests "$OUT/shots-v019"
[ -x $B/tests/qb_v020_tests ] && run v020 $B/tests/qb_v020_tests "$OUT/shots-v020"
[ -x $B/tests/qb_v021_tests ] && run v021 $B/tests/qb_v021_tests "$OUT/shots-v021"
[ -x $B/tests/qb_v0211_tests ] && run v0211 $B/tests/qb_v0211_tests "$OUT/shots-v0211"
[ -x $B/tests/qb_v0212_tests ] && run v0212 $B/tests/qb_v0212_tests "$OUT/shots-v0212"
[ -x $B/tests/qb_v040_tests ] && run v040 $B/tests/qb_v040_tests "$OUT/shots-v040"
[ -x $B/tests/qb_v0401_tests ] && run v0401 $B/tests/qb_v0401_tests "$OUT/shots-v0401"
[ -x $B/tests/qb_v0402_tests ] && run v0402 $B/tests/qb_v0402_tests "$OUT/shots-v0402"
[ -x $B/tests/qb_v0403_tests ] && run v0403 $B/tests/qb_v0403_tests "$OUT/shots-v0403"
[ -x $B/tests/qb_v050_tests ] && run v050 $B/tests/qb_v050_tests "$OUT/shots-v050"
[ -x $B/tests/qb_v060_tests ] && run v060 $B/tests/qb_v060_tests "$OUT/shots-v060"
[ -x $B/tests/qb_menu_audit ] && run menu_audit $B/tests/qb_menu_audit "$OUT/shots-audit"
exit 0
