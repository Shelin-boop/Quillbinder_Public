#!/bin/bash
# Runs every test suite of Quillbinder and prints one line per suite.
#   tools/run-all-tests.sh [output dir] [offscreen|x11]
# Needs a build in build/ with the test switches (cmake -S . -B build -DQB_TEST_SWITCHES=ON && cmake --build build -j"$(nproc)").
# Logs: <output dir>/<suite>.<mode>.log, screenshots: <output dir>/shots-<suite>/.
# The add-on packages (the media add-ons need FFmpeg) are built first when an FFmpeg
# prefix is there: QB_FFMPEG_DIR and QB_FFMPEG_FULL_DIR (default build/ffmpeg and
# build/ffmpeg-full, see tools/get-ffmpeg.sh).
# A suite passes when its last line says "0 failure(s)" and rc=0.
# The exit status (0.65.0, audit F08): 1 when a suite failed, timed out, has no summary line or was not
# built; the last lines name them ("failed: ...", "not built: ...").
# Only some suites (names as printed below: qb_tests, ui, m6 ... m14, fixes, v017 ...):
#   QB_SUITES="v040 m13"   run these
#   QB_SAMPLE=10           add whole suites drawn at random until about 10 % of the checks are covered
#                          (counts from tests/check-counts.txt); QB_SEED=<n> repeats a draw
#   QB_SKIP_SUITES="v060"  leave these out (0.70.0: the release workflow, where the public copy has no private files)
# A full run in x11 mode (neither variable set) writes tests/check-counts.txt anew.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# 0.62: programs started by the suites skip the questions of the first start
export QB_SKIP_FIRST_START=1
OUT=${1:-$ROOT/build/test-results}
MODE=${2:-offscreen}
export QB_FFMPEG_DIR=${QB_FFMPEG_DIR:-$ROOT/build/ffmpeg}
export QB_FFMPEG_FULL_DIR=${QB_FFMPEG_FULL_DIR:-$ROOT/build/ffmpeg-full}   # (0.50: the add-on "Extra Video and Audio Formats")
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$ROOT" || exit 1
B=build
if [ ! -x $B/tests/qb_tests ]; then
    echo "no build in $ROOT/$B: cmake -S . -B build -DQB_TEST_SWITCHES=ON && cmake --build build -j\$(nproc)"
    exit 1
fi
# 0.70.0 (audit A S-04): the switches are off by default; the suites need them
if ! grep -q '^QB_TEST_SWITCHES:BOOL=ON' "$B/CMakeCache.txt" 2>/dev/null; then
    echo "the build in $ROOT/$B has no test switches: cmake -S . -B build -DQB_TEST_SWITCHES=ON, then build again"
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
elif [ "$MODE" = x11 ] && [ -z "$QB_SKIP_SUITES" ]; then
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
        packaging/linux/build-addons.sh $B $B/addons-dist > "$OUT/addons.log" 2>&1 || { echo "add-ons failed"; tail "$OUT/addons.log"; }
        # the template files in the repository are made anew with new time stamps: keep the committed ones
        git checkout -- addons/*/payload/templates/ 2>/dev/null
    fi
else
    echo "(no FFmpeg in $QB_FFMPEG_DIR: add-ons not built, the media checks of m11 may fail)"
fi
# a sound server for the media tests (a null sink is enough). 0.70.0: started without the system's default.pa (-n),
# with only a null sink and the local socket: in a container (GitHub's runner) a module of default.pa that cannot load
# there could end the server, and the media checks failed; what the server says is kept in pulseaudio.log
# The server gets a home and runtime folder of its own (made here, so owned by this user): it refuses folders owned by
# another user, as the home folder of GitHub's container job may be.
PULSE_DIR=
if command -v pactl > /dev/null && ! pactl info > /dev/null 2>&1; then
    PULSE_DIR=$(mktemp -d /tmp/qbpulse.XXXX)
    HOME=$PULSE_DIR XDG_CONFIG_HOME=$PULSE_DIR/config XDG_RUNTIME_DIR=$PULSE_DIR \
        pulseaudio -n -D --exit-idle-time=-1 --system=false --log-target=stderr \
        --load="module-null-sink sink_name=qbnull" --load="module-native-protocol-unix" > "$OUT/pulseaudio.log" 2>&1 \
        || pulseaudio -D --exit-idle-time=-1 --system=false >> "$OUT/pulseaudio.log" 2>&1 || true
    # 0.90.1: wait for its socket (up to 10 s; 2 s were not always enough, and an old socket of a server that is gone
    # was taken instead: the microphone checks of v061 found "Connection refused")
    for _ in $(seq 1 20); do [ -S "$PULSE_DIR/pulse/native" ] && break; sleep 0.5; done
fi
PS=$(ls -d ${PULSE_DIR:+"$PULSE_DIR"/pulse/native} /tmp/pulse-*/native "${XDG_RUNTIME_DIR:-/nonexistent}"/pulse/native 2>/dev/null | head -1)
[ -n "$PS" ] && export PULSE_SERVER=unix:$PS
if command -v pactl > /dev/null; then
    if pactl info > /dev/null 2>&1; then
        echo "(sound server: $(pactl info 2>/dev/null | sed -n 's/^Default Sink: //p'))"
    else
        echo "(no sound server: the media checks will fail; $(tail -n 3 "$OUT/pulseaudio.log" 2>/dev/null | tr '\n' ' '))"
    fi
fi
run() {
    local name=$1; shift
    [ -n "$SELECTED" ] && [[ " $SELECTED " != *" $name "* ]] && return
    [[ " $QB_SKIP_SUITES " == *" $name "* ]] && { echo "$name skipped (QB_SKIP_SUITES)"; return; }
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
    # 0.65.0 (audit F08): the result counts for the exit status
    if [ "$rc" != 0 ] || [ "$fail" != 0 ] || [ -z "$summary" ] || [[ "$summary" != "0 failure(s)"* ]]; then
        FAILED="$FAILED $name"
    fi
}
# a suite whose program is not there counts as failed when it would run (0.65.0, audit F08: it was skipped silently)
runx() {   # runx <name> <program to look for> <command ...>
    local name=$1 prog=$2
    shift 2
    [ -n "$SELECTED" ] && [[ " $SELECTED " != *" $name "* ]] && return
    [[ " $QB_SKIP_SUITES " == *" $name "* ]] && { echo "$name skipped (QB_SKIP_SUITES)"; return; }
    if [ ! -x "$prog" ]; then
        echo "$name not built ($prog)"
        MISSING="$MISSING $name"
        return
    fi
    run "$name" "$@"
}
FAILED=""
MISSING=""
run qb_tests $B/tests/qb_tests
run ui $B/tests/qb_ui_tests "$OUT/shots-ui"
for m in 6 7 8 9 10 11 12 13 14; do   # (m15, the tutorial of 0.15 to 0.30, is gone)
    runx m$m $B/tests/qb_m${m}_tests $B/tests/qb_m${m}_tests "$OUT/shots-m$m"
done
runx fixes $B/tests/qb_fixes_tests $B/tests/qb_fixes_tests "$OUT/shots-fixes"
runx v017 $B/tests/qb_v017_tests $B/tests/qb_v017_tests "$OUT/shots-v017"
runx v018 $B/tests/qb_v018_tests $B/tests/qb_v018_tests "$OUT/shots-v018"
runx v019 $B/tests/qb_v019_tests $B/tests/qb_v019_tests "$OUT/shots-v019"
runx v020 $B/tests/qb_v020_tests $B/tests/qb_v020_tests "$OUT/shots-v020"
runx v021 $B/tests/qb_v021_tests $B/tests/qb_v021_tests "$OUT/shots-v021"
runx v0211 $B/tests/qb_v0211_tests $B/tests/qb_v0211_tests "$OUT/shots-v0211"
runx v0212 $B/tests/qb_v0212_tests $B/tests/qb_v0212_tests "$OUT/shots-v0212"
runx v040 $B/tests/qb_v040_tests $B/tests/qb_v040_tests "$OUT/shots-v040"
runx v0401 $B/tests/qb_v0401_tests $B/tests/qb_v0401_tests "$OUT/shots-v0401"
runx v0402 $B/tests/qb_v0402_tests $B/tests/qb_v0402_tests "$OUT/shots-v0402"
runx v0403 $B/tests/qb_v0403_tests $B/tests/qb_v0403_tests "$OUT/shots-v0403"
runx v050 $B/tests/qb_v050_tests $B/tests/qb_v050_tests "$OUT/shots-v050"
runx v060 $B/tests/qb_v060_tests $B/tests/qb_v060_tests "$OUT/shots-v060"
runx v0601 $B/tests/qb_v0601_tests $B/tests/qb_v0601_tests "$OUT/shots-v0601"
runx v0602 $B/tests/qb_v0602_tests $B/tests/qb_v0602_tests "$OUT/shots-v0602"
runx v0603 $B/tests/qb_v0603_tests $B/tests/qb_v0603_tests "$OUT/shots-v0603"
runx v0604 $B/tests/qb_v0604_tests $B/tests/qb_v0604_tests "$OUT/shots-v0604"
runx v0605 $B/tests/qb_v0605_tests $B/tests/qb_v0605_tests "$OUT/shots-v0605"
runx v061 $B/tests/qb_v061_tests $B/tests/qb_v061_tests "$OUT/shots-v061"
run v0611 $B/tests/qb_v0611_tests "$OUT/shots-v0611"
run v062 $B/tests/qb_v062_tests "$OUT/shots-v062"
runx v064 $B/tests/qb_v064_tests $B/tests/qb_v064_tests "$OUT/shots-v064"
runx v0641 $B/tests/qb_v0641_tests $B/tests/qb_v0641_tests "$OUT/shots-v0641"
runx v0642 $B/tests/qb_v0642_tests $B/tests/qb_v0642_tests "$OUT/shots-v0642"
runx v0650 $B/tests/qb_v0650_tests $B/tests/qb_v0650_tests "$OUT/shots-v0650"
runx v0700 $B/tests/qb_v0700_tests $B/tests/qb_v0700_tests "$OUT/shots-v0700"
runx v0810 $B/tests/qb_v0810_tests $B/tests/qb_v0810_tests "$OUT/shots-v0810"
runx v0811 $B/tests/qb_v0811_tests $B/tests/qb_v0811_tests "$OUT/shots-v0811"
runx v0812 $B/tests/qb_v0812_tests $B/tests/qb_v0812_tests "$OUT/shots-v0812"
runx v0813 $B/tests/qb_v0813_tests $B/tests/qb_v0813_tests "$OUT/shots-v0813"
runx v0900 $B/tests/qb_v0900_tests $B/tests/qb_v0900_tests "$OUT/shots-v0900"
runx v0901 $B/tests/qb_v0901_tests $B/tests/qb_v0901_tests "$OUT/shots-v0901"
runx v0902 $B/tests/qb_v0902_tests $B/tests/qb_v0902_tests "$OUT/shots-v0902"
runx v0903 $B/tests/qb_v0903_tests $B/tests/qb_v0903_tests "$OUT/shots-v0903"
runx audit1 $B/tests/qb_audit1_tests $B/tests/qb_audit1_tests "$OUT/shots-audit1"
runx fuzz $B/tests/qb_fuzz_tests $B/tests/qb_fuzz_tests "$OUT/shots-fuzz"
runx w2 $B/tests/qb_w2_tests $B/tests/qb_w2_tests "$OUT/shots-w2"
runx w3 $B/tests/qb_w3_tests $B/tests/qb_w3_tests "$OUT/shots-w3"
runx w4 $B/tests/qb_w4_tests $B/tests/qb_w4_tests "$OUT/shots-w4"
runx w4c $B/tests/qb_w4c_tests $B/tests/qb_w4c_tests "$OUT/shots-w4c"
# a suite that was asked for and is not there at all (a name mistyped in QB_SUITES)
for n in $SELECTED; do
    [[ " $RAN $MISSING " == *" $n "* ]] || MISSING="$MISSING $n"
done
if [ -n "$FAILED$MISSING" ]; then
    [ -n "$FAILED" ] && echo "failed:$FAILED"
    [ -n "$MISSING" ] && echo "not built:$MISSING"
    exit 1
fi
echo "all suites passed"
exit 0
