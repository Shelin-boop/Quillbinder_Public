#!/bin/bash
# The system test (crash safety and usability): only when the writer asks for it.
# Not called by tools/run-all-tests.sh; its checks are not in tests/check-counts.txt.
# Specification: the project document claude/quillbinder-test-procedure-crashsafe.md.
# The whole system test in one call (setup, build, every case): tools/run-system-test.sh.
#
#   tools/run-crashsafe.sh [case or group ...]      e.g. "S-T01", "S-T" (all S-T cases); none = every case
#
#   QB_CS_PROFILE=quick|full|smoke   quick is the standard (the procedure, decision 2); smoke = a shakedown
#                              of the harness (shortest typing runs and loops)
#   QB_CS_SEED=<n>             repeats the random parts of a run
#   QB_CS_OUT=<dir>            results (default build/crashsafe-results); fixtures are kept there and
#                              made again only when <dir>/fixtures/done is missing (QB_CS_FIXTURES: elsewhere)
#   QB_CS_NO_FX2=1             without the large fixtures (S-D01, X-02's last round need them)
#   QB_CS_BUDGET_MIN=<n>       minutes this call may take (default 100): no case is started that would
#                              not end in time, so a background command (2 h at most) is never cut off
#   QB_CS_FRESH=1              start again: forget the cases done in <dir>
#   QB_CS_JOBS=<n>             cases run side by side (default 1; more only for a shakedown: the timings
#                              and the typing gates need the machine to themselves)
#
# Resuming: every case that ended writes <dir>/<id>/result.line; a later call with the same <dir> and
# profile goes on with the cases that have none. The call ends with "crashsafe rc=3" (more to do: call
# again, same command) or "crashsafe rc=0" (all done; the summary of every case follows).
#
# One line per case like the test runner: "<id> rc=.. verify=.. pass=.. fail=.. time=..s <state>",
# then its FAIL lines; crash and freeze reports are copied into <dir>/<id>/reports. Logs stay in files.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
B=${QB_BUILD:-build}
BIN=$B/tests/qb_crashsafe_tests
OUT=${QB_CS_OUT:-$B/crashsafe-results}
[ -x "$BIN" ] || { echo "no $BIN: build first (cmake --build $B)"; exit 2; }
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
export QB_CS_PROFILE=${QB_CS_PROFILE:-quick}
export QB_SKIP_FIRST_START=1
export QB_CS_FIXTURES=${QB_CS_FIXTURES:-$OUT/fixtures}
QB_SCRIV_SAMPLE=${QB_SCRIV_SAMPLE:-}
BUDGET=$(( ${QB_CS_BUDGET_MIN:-100} * 60 ))
JOBS=${QB_CS_JOBS:-1}
CALL_START=$(date +%s)
SCREEN="-screen 0 1920x1080x24"

# a run is one profile in one results folder
if [ "${QB_CS_FRESH:-0}" = 1 ] || { [ -f "$OUT/profile" ] && [ "$(cat "$OUT/profile")" != "$QB_CS_PROFILE" ]; }; then
    find "$OUT" -mindepth 2 -maxdepth 2 -name result.line -delete 2>/dev/null
    rm -f "$OUT/summary.txt"
fi
echo "$QB_CS_PROFILE" > "$OUT/profile"

# a sound server (a null sink), as tools/run-all-tests.sh sets up
if command -v pactl > /dev/null && ! pactl info > /dev/null 2>&1; then
    pulseaudio -D --exit-idle-time=-1 --system=false > /dev/null 2>&1 || true
    sleep 2
fi
PS=$(ls -d /tmp/pulse-*/native 2>/dev/null | head -1)
[ -n "$PS" ] && export PULSE_SERVER=unix:$PS

# the add-ons ([A], [S]), when they are not built yet
if [ ! -f "$B/addons-dist/speech-transcription.qbpkg" ] && [ -d "$B/whisper" ]; then
    echo "building the add-ons (log: $OUT/addons.log)"
    QB_FFMPEG_DIR=${QB_FFMPEG_DIR:-$ROOT/$B/ffmpeg} QB_FFMPEG_FULL_DIR=${QB_FFMPEG_FULL_DIR:-$ROOT/$B/ffmpeg-full} \
        packaging/build-addons.sh "$B" "$B/addons-dist" > "$OUT/addons.log" 2>&1 || echo "add-ons failed (see the log)"
    git checkout -q -- addons/*/payload/templates/ 2>/dev/null
fi

# which cases
mapfile -t ALL < <("$BIN" --list)
SELECTED=()
for line in "${ALL[@]}"; do
    id=${line%% *}
    if [ $# -eq 0 ]; then
        SELECTED+=("$line")
    else
        for want in "$@"; do
            if [ "$id" = "$want" ] || [[ "$id" == "$want"[0-9]* ]]; then
                SELECTED+=("$line")
                break
            fi
        done
    fi
done
[ ${#SELECTED[@]} -gt 0 ] || { echo "no case matches: $*"; exit 2; }

# FX-3 [N]: the public Scrivener sample (S-P04, S-P08, C-37), fetched once when the network allows it,
# at the commit the normal suites pin (tests/scrivener_checks.cpp checks a hash over its files)
if [ -z "$QB_SCRIV_SAMPLE" ] && [ ! -d "$OUT/fx3/tests/sample-project.scriv" ]; then
    rm -rf "$OUT/fx3"
    timeout 180 git clone -q --filter=blob:none --no-checkout https://github.com/writerslogic/scrivener-mcp "$OUT/fx3" > /dev/null 2>&1 \
        && (cd "$OUT/fx3" && timeout 120 git sparse-checkout set tests/sample-project.scriv > /dev/null 2>&1 \
            && timeout 120 git checkout -q d4d42c3 > /dev/null 2>&1) \
        || echo "FX-3: the public Scrivener sample could not be fetched (S-P04, S-P08, C-37 use what is there or are left out)"
fi
[ -z "$QB_SCRIV_SAMPLE" ] && [ -d "$OUT/fx3/tests/sample-project.scriv" ] && QB_SCRIV_SAMPLE="$OUT/fx3/tests/sample-project.scriv"
export QB_SCRIV_SAMPLE

# the fixtures, once per results folder
if [ ! -f "$QB_CS_FIXTURES/done" ]; then
    rm -rf "$QB_CS_FIXTURES"
    mkdir -p "$QB_CS_FIXTURES"
    h=$(mktemp -d /tmp/qbcs.XXXX)
    start=$(date +%s)
    HOME=$h XDG_RUNTIME_DIR=$h timeout 1800 xvfb-run -a -s "$SCREEN" "$BIN" --fixtures "$QB_CS_FIXTURES" > "$OUT/fixtures.log" 2>&1 \
        && touch "$QB_CS_FIXTURES/done"
    echo "fixtures rc=$? time=$(( $(date +%s) - start ))s ($(grep -c '' "$OUT/fixtures.log") log lines)"
    rm -rf "$h"
    [ -f "$QB_CS_FIXTURES/done" ] || { tail -5 "$OUT/fixtures.log"; exit 1; }
fi

# ends a display server that still holds display :n, and removes its lock
free_display() {
    local n=$1 p
    if [ -f "/tmp/.X$n-lock" ]; then
        p=$(tr -d ' ' < "/tmp/.X$n-lock")
        if [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then
            kill "$p" 2>/dev/null
            for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$p" 2>/dev/null || break; sleep 0.3; done
            kill -9 "$p" 2>/dev/null
        fi
        rm -f "/tmp/.X$n-lock" "/tmp/.X11-unix/X$n"
    fi
}

# runs one case; writes <dir>/<id>/result.line and prints it with the FAIL lines
run_case() {
    local line=$1 slot=$2 limit=$3
    local id quick full needs title
    read -r id quick full needs title <<< "$line"
    local d=$OUT/$id
    rm -rf "$d"
    mkdir -p "$d"
    local h
    h=$(mktemp -d /tmp/qbcs.XXXX)
    local start
    start=$(date +%s)
    # a display of the slot's own (xvfb-run -a can hand two cases started together the same number);
    # a display server that a case or a killed run left behind is ended first (shakedown 2026-11-09:
    # one left on :90 and :91 made every later case fail to start)
    local n=$((90 + slot))
    free_display "$n"
    HOME=$h XDG_RUNTIME_DIR=$h timeout -k 30 "$limit" xvfb-run -n $n -s "$SCREEN" \
        sh -c 'setxkbmap de 2>/dev/null; exec "$0" "$@"' "$BIN" --case "$id" "$d" > "$d/case.log" 2>&1
    local rc=$?
    free_display "$n"
    if grep -q '^xvfb-run: error' "$d/case.log" && ! grep -q '^case ' "$d/case.log"; then
        # the case never started: no result, so the next call runs it again
        echo "$id not started: the virtual display did not start (it runs again in the next call)"
        rm -rf "$h"
        return
    fi
    HOME=$h XDG_RUNTIME_DIR=$h QT_QPA_PLATFORM=offscreen timeout 600 "$BIN" --verify "$d" > "$d/verify.log" 2>&1
    local vrc=$?
    local state=""
    if [ $rc -eq 124 ] || [ $rc -eq 137 ]; then
        state="FROZEN (no end within ${limit}s: G2)"
    elif ! grep -q "^END $id" "$d/case.log"; then
        state="CRASHED (no end line: G1)"
    fi
    grep -q "^SKIP" "$d/case.log" && state="skipped: $(grep -m1 '^SKIP' "$d/case.log" | cut -c7-)"
    local pass fail
    pass=$(cat "$d/case.log" "$d/verify.log" | grep -c "^PASS")
    fail=$(cat "$d/case.log" "$d/verify.log" | grep -c "^FAIL")
    [ -n "$state" ] && [[ "$state" != skipped* ]] && fail=$((fail + 1))
    {
        echo "$id rc=$rc verify=$vrc pass=$pass fail=$fail time=$(( $(date +%s) - start ))s $state"
        grep -h "^FAIL" "$d/case.log" "$d/verify.log" | head -8 | sed 's/^/    /'
        grep -h "^UNANSWERED" "$d/case.log" | head -4 | sed 's/^/    /' | cut -c1-220
        grep -h "G7 .*writer: Pass\|G7 .*writer: Fail" "$d/case.log" | sed 's/^ */    /' | cut -c1-220
        if [ -d "$h/.local/share/Quillbinder/CrashReports" ] && [ -n "$(ls -A "$h/.local/share/Quillbinder/CrashReports" 2>/dev/null)" ]; then
            mkdir -p "$d/reports"
            cp "$h/.local/share/Quillbinder/CrashReports/"* "$d/reports/" 2>/dev/null
            ls "$d/reports" | sed "s|^|    report: $d/reports/|"
        fi
    } > "$d/result.tmp"
    mv "$d/result.tmp" "$d/result.line"
    cat "$d/result.line"
    cat "$d/result.line" >> "$OUT/summary.txt"
    rm -rf "$h"
}

TODO=()
for line in "${SELECTED[@]}"; do
    id=${line%% *}
    [ -f "$OUT/$id/result.line" ] || TODO+=("$line")
done
DONE_BEFORE=$(( ${#SELECTED[@]} - ${#TODO[@]} ))
echo "system test, profile $QB_CS_PROFILE${QB_CS_SEED:+, seed $QB_CS_SEED}: ${#SELECTED[@]} case(s), $DONE_BEFORE done before, ${#TODO[@]} to run; budget $((BUDGET / 60)) min, $JOBS at a time"

declare -A SLOT_PID=()
started=0
for line in "${TODO[@]}"; do
    read -r id quick full needs title <<< "$line"
    est=$quick
    [ "$QB_CS_PROFILE" = full ] && est=$full
    [ "$QB_CS_PROFILE" = smoke ] && est=$(( quick / 2 + 30 ))
    limit=$(( est * 3 ))
    [ $limit -lt 300 ] && limit=300
    elapsed=$(( $(date +%s) - CALL_START ))
    # start a case only when its estimate (and the verify step) fits the budget; the first one always
    if [ $started -gt 0 ] && [ $(( elapsed + est * 3 / 2 + 120 )) -gt $BUDGET ]; then
        break
    fi
    # never longer than the background command allows (2 h): the rest of the budget plus 10 min
    cap=$(( BUDGET + 600 - elapsed ))
    [ $limit -gt $cap ] && limit=$cap
    # a free slot
    while :; do
        free=""
        for ((s = 0; s < JOBS; s++)); do
            p=${SLOT_PID[$s]:-}
            if [ -z "$p" ] || ! kill -0 "$p" 2>/dev/null; then free=$s; break; fi
        done
        [ -n "$free" ] && break
        wait -n 2>/dev/null || true
    done
    if [ "$JOBS" -gt 1 ]; then
        run_case "$line" "$free" "$limit" &
        SLOT_PID[$free]=$!
    else
        run_case "$line" 0 "$limit"
    fi
    started=$((started + 1))
done
wait

left=0
for line in "${SELECTED[@]}"; do
    id=${line%% *}
    [ -f "$OUT/$id/result.line" ] || left=$((left + 1))
done
took=$(( ($(date +%s) - CALL_START) / 60 ))
if [ $left -gt 0 ]; then
    echo "system test: $(( ${#SELECTED[@]} - left )) of ${#SELECTED[@]} case(s) done, $left left (this call ${took} min); call again with the same command to go on"
    echo "crashsafe rc=3"
    exit 3
fi
cases_failed=0
total_fail=0
for line in "${SELECTED[@]}"; do
    id=${line%% *}
    f=$(head -1 "$OUT/$id/result.line" | sed -n 's/.* fail=\([0-9]*\).*/\1/p')
    total_fail=$((total_fail + ${f:-0}))
    [ "${f:-0}" -gt 0 ] && cases_failed=$((cases_failed + 1))
done
skipped=$(for line in "${SELECTED[@]}"; do head -1 "$OUT/${line%% *}/result.line"; done | grep -c ' skipped: ')
echo "system test done: ${#SELECTED[@]} case(s), $cases_failed with failures ($total_fail failure(s)), $skipped skipped; results in $OUT"
echo "cases with failures:"
for line in "${SELECTED[@]}"; do head -1 "$OUT/${line%% *}/result.line"; done | grep -v ' fail=0 ' | sed 's/^/    /'
echo "crashsafe rc=0"
