#!/bin/bash
# The whole system test in one command (2026-11-09, the writer's wish: "the complete set at once, with one
# call"): setup, build, fixtures and every case, in calls of about 90 minutes each (at most 100), so that a
# background command of the cloud session (2 h at most) is never cut off.
#
#   QB_CS_PROFILE=full tools/run-system-test.sh      the next run the writer asked for (all cases, full profile)
#   tools/run-system-test.sh                         the standard, quick profile
#
# Call it again with the same command until it ends with "system test rc=0"; "system test rc=3" means
# there is more to do (the setup, the build or cases were left for the next call). Each call goes on where
# the last one stopped: setup steps already done are skipped, the build is incremental, and cases that
# ended keep their results (tools/run-crashsafe.sh). QB_CS_FRESH=1 on the first call starts a run anew.
#
# Root only (the cloud session). Logs: build/system-test-logs/. The results: build/crashsafe-results
# (QB_CS_OUT), with summary.txt holding one line per case.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
B=${QB_BUILD:-build}
LOG=$B/system-test-logs
mkdir -p "$LOG"
START=$(date +%s)
# 90 minutes a call by default: a background command ends after 2 h, and a case may run past the
# budget (its own estimate can be too short); QB_ST_CALL_MIN changes it
LIMIT=$(( ${QB_ST_CALL_MIN:-90} * 60 ))
export QB_CS_PROFILE=${QB_CS_PROFILE:-quick}
mins() { echo $(( ($(date +%s) - START) / 60 )); }
more() { echo "$1"; echo "system test: call again with the same command to go on (this call $(mins) min)"; echo "system test rc=3"; exit 3; }
step() {   # step <marker> <minutes it may take> <title> <command...>
    local mark=$1 need=$2 title=$3
    shift 3
    [ -f "$B/.st-$mark" ] && return 0
    [ $(( $(date +%s) - START + need * 60 )) -gt "$LIMIT" ] && more "next: $title"
    local s
    s=$(date +%s)
    if "$@" > "$LOG/$mark.log" 2>&1; then
        touch "$B/.st-$mark"
        echo "$title: done in $(( ($(date +%s) - s) / 60 )) min"
    else
        echo "$title: FAILED (log: $LOG/$mark.log)"
        tail -n 5 "$LOG/$mark.log" | sed 's/^/    /'
        echo "system test rc=1"
        exit 1
    fi
}
mkdir -p "$B"

# 1. setup: the tools, LibreOffice and epubcheck [L], the German dictionary [G], whisper.cpp [S], FFmpeg [A]
step setup 15 "setup (tools, LibreOffice, epubcheck)" env QB_FULL_SETUP=1 bash tools/cloud-setup.sh
step german 3 "German dictionary" apt-get install -y -qq hunspell-de-de
[ -d "$B/whisper" ] && touch "$B/.st-whisper"
step whisper 15 "whisper.cpp with Vulkan" bash tools/get-whisper.sh
[ -d "$B/ffmpeg" ] && [ -d "$B/ffmpeg-full" ] && touch "$B/.st-ffmpeg"
step ffmpeg 20 "FFmpeg" bash tools/get-ffmpeg.sh

# 2. build with the tests (incremental; the first build takes 8 to 20 minutes)
if [ ! -f "$B/build.ninja" ]; then
    cmake -S . -B "$B" -G Ninja > "$LOG/configure.log" 2>&1 || { echo "configure FAILED (log: $LOG/configure.log)"; echo "system test rc=1"; exit 1; }
fi
[ $(( $(date +%s) - START + 25 * 60 )) -gt "$LIMIT" ] && more "next: the build"
s=$(date +%s)
if cmake --build "$B" -j"$(nproc)" > "$LOG/build.log" 2>&1; then
    echo "build: done in $(( ($(date +%s) - s) / 60 )) min"
else
    echo "build FAILED (log: $LOG/build.log)"
    grep -m5 'error' "$LOG/build.log" | sed 's/^/    /'
    echo "system test rc=1"
    exit 1
fi
[ -f "$B/whisper/vulkan.txt" ] && ! grep -q ON "$B/whisper/vulkan.txt" && echo "note: whisper.cpp is built without Vulkan ($B/whisper/vulkan.txt)"

# 3. the cases, with what is left of this call (the fixtures count against it on the first call)
left=$(( (LIMIT - ($(date +%s) - START)) / 60 - 5 ))
[ $left -lt 20 ] && more "next: the cases"
QB_CS_BUDGET_MIN=$left tools/run-crashsafe.sh "$@"
rc=$?
if [ $rc -eq 3 ]; then
    echo "system test: call again with the same command to go on (this call $(mins) min)"
    echo "system test rc=3"
    exit 3
fi
[ $rc -eq 0 ] && { echo "system test rc=0"; exit 0; }
echo "system test rc=$rc"
exit $rc
