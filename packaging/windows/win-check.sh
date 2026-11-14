#!/usr/bin/env bash
# Checks of the Windows program on the runner (port, W2, 2026-11-07): the program update loader,
# crash and freeze reports, sessions, the instance link of the writing tools, add-ons.
# Runs in Git Bash on the Windows runner (packaging/windows/win-ci.sh) with the packed program folder.
#
#   packaging/windows/win-check.sh <program folder> <add-on folder> <results dir>
#
# Prints one line per check ("check <name>: ok" or "check <name>: FAIL <why>") and at the end
# "checks: <n> ok, <m> failed". Uses the program's own data folder (W4b, the portable program: "data" beside
# quillbinder.exe; give it a copy of the program folder), which it empties first. Every start is offscreen and limited
# in time.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIST=$(cd "$1" && pwd)
ADDONS=$(cd "$2" && pwd)
OUT=$(cd "$3" && pwd)
LOG="$OUT/checks.log"
: > "$LOG"
PY=$(command -v python3 || command -v python)
EXE="$DIST/quillbinder.exe"
DATA="$DIST/data"
REPORTS="$DATA/CrashReports"
SESSIONS="$DATA/sessions"
ok=0
failed=0
pass() { echo "check $1: ok" | tee -a "$LOG"; ok=$((ok + 1)); }
fail() { echo "check $1: FAIL $2" | tee -a "$LOG"; failed=$((failed + 1)); }
run() { # <seconds> <output file> <args...>: the program, offscreen, without dialogs
    local secs=$1 out=$2
    shift 2
    ( cd "$DIST" && QT_QPA_PLATFORM=offscreen QB_NO_DIALOGS=1 timeout "$secs" "$EXE" "$@" > "$out" 2>&1 )
}
win() { cygpath -w "$1"; }

rm -rf "$DATA"
taskkill //F //IM quillbinder.exe > /dev/null 2>&1

# ---- the version
run 60 "$OUT/c-version.txt" --version
grep -q "^Quillbinder " "$OUT/c-version.txt" && pass version || fail version "$(head -c 200 "$OUT/c-version.txt")"
ver=$(sed -n 's/^Quillbinder \([0-9.]*\).*/\1/p' "$OUT/c-version.txt" | tr -d '\r')
runtime=$(sed -n 's/^  set(APP_RUNTIME "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")

# ---- a program update: the same program packed as a newer version, installed with --install
#      (which starts it once invisibly), then started through the loader
upd="$OUT/c-update"
rm -rf "$upd" && mkdir -p "$upd"
newer="${ver%.*}.$(( ${ver##*.} + 1 ))"
cp "$EXE" "$upd/quillbinder.exe"
"$PY" "$ROOT/tools/make-package.py" core --binary "$(win "$upd/quillbinder.exe")" --version "$newer" --runtime "$runtime" \
    --name "Check update" "$(win "$upd/check-update.qbpkg")" > /dev/null
run 180 "$OUT/c-install.txt" --install "$(win "$upd/check-update.qbpkg")" --allow-unsigned
rc=$?
if [ $rc -eq 0 ] && grep -q "Program version $newer installed" "$OUT/c-install.txt"; then pass update-install
else fail update-install "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-install.txt" | head -c 400)"; fi
[ -f "$DATA/core/$newer/qt.conf" ] && pass update-qtconf || fail update-qtconf "no qt.conf beside the update"
( cd "$DIST" && QB_DEBUG_ENV=1 timeout 120 "$EXE" --versions > "$OUT/c-versions.txt" 2>&1 )
rc=$?
tr -d '\r' < "$OUT/c-versions.txt" > "$OUT/c-versions.clean"
if grep -q "overlay=\[1\]" "$OUT/c-versions.clean" && grep -q "QB_CORE_APPDIR=\[\]" "$OUT/c-versions.clean" \
   && grep -q "path-starts-with-program=\[0\]" "$OUT/c-versions.clean" && grep -q "^$newer.*(active)" "$OUT/c-versions.clean"; then
    pass update-loader
else
    fail update-loader "rc=$rc $(tr '\n' ' ' < "$OUT/c-versions.clean" | head -c 500)"
fi
QB_TEST_CRASH=quit run 180 "$OUT/c-update-window.txt" --no-restore
rc=$?
attempts=$("$PY" -c "import json,sys; print(json.load(open(sys.argv[1])).get('attempts', 0))" "$(win "$DATA/core/state.json")" 2>/dev/null)
if [ $rc -eq 0 ] && [ "$attempts" = "0" ]; then pass update-window
else fail update-window "rc=$rc attempts=$attempts $(tr '\r\n' '  ' < "$OUT/c-update-window.txt" | head -c 400)"; fi
# a damaged update is switched off and the built-in version runs
echo "damaged" >> "$DATA/core/$newer/quillbinder.exe"
( cd "$DIST" && QB_DEBUG_ENV=1 timeout 120 "$EXE" --versions > "$OUT/c-damaged.txt" 2>&1 )
if tr -d '\r' < "$OUT/c-damaged.txt" | grep -q "overlay=\[0\]"; then pass update-damaged
else fail update-damaged "$(tr '\r\n' '  ' < "$OUT/c-damaged.txt" | head -c 300)"; fi
rm -rf "$DATA/core"

# ---- crash reports
crash_check() { # <name> <QB_TEST_CRASH value> <expected Signal line part>
    local name=$1 kind=$2 expect=$3
    rm -rf "$REPORTS" "$SESSIONS"
    QB_TEST_CRASH=$kind run 120 "$OUT/c-$name.txt" --no-restore
    local rc=$?
    local rep
    rep=$(ls "$REPORTS"/crash-*.txt 2>/dev/null | head -1)
    if [ -z "$rep" ]; then fail "$name" "rc=$rc, no report; $(tr '\r\n' '  ' < "$OUT/c-$name.txt" | head -c 300)"; return; fi
    cp "$rep" "$OUT/report-$name.txt"
    local dmp=${rep%.txt}.dmp
    local why=""
    grep -q "^Signal: .*$expect" "$rep" || why="$why no '$expect';"
    grep -q "(the main thread)" "$rep" || why="$why not the main thread;"
    sed -n '/^== Backtrace/,/^== Program files/p' "$rep" | grep -q "quillbinder.exe+0x" || why="$why no program frame in the backtrace;"
    grep -q "check of the crash guard: $kind" "$rep" || why="$why recent action missing;"
    [ -s "$dmp" ] || why="$why no memory dump;"
    ls "$SESSIONS"/*.json > /dev/null 2>&1 || why="$why session marker gone;"
    [ -z "$why" ] && pass "$name" || fail "$name" "rc=$rc$why"
}
crash_check crash-access access EXCEPTION_ACCESS_VIOLATION
crash_check crash-abort abort SIGABRT

# ---- a freeze the program comes out of
rm -rf "$REPORTS"
QB_TEST_CRASH=freeze run 180 "$OUT/c-freeze.txt" --no-restore
rc=$?
rep=$(ls "$REPORTS"/freeze-*.txt 2>/dev/null | head -1)
if [ -n "$rep" ]; then
    cp "$rep" "$OUT/report-freeze.txt"
    why=""
    [ $rc -eq 0 ] || why="$why rc=$rc;"
    grep -q "^== Responded again after" "$rep" || why="$why no 'Responded again';"
    sed -n '/^== Where the main thread is/,/^== Program files/p' "$rep" | grep -q "+0x" || why="$why no frames;"
    [ -z "$why" ] && pass freeze || fail freeze "$why"
else
    fail freeze "rc=$rc, no report"
fi

# ---- sessions: the markers of the crashed sessions go at the next start; a normal end leaves none
QB_TEST_CRASH=access run 120 "$OUT/c-session-crash.txt" --no-restore
QB_TEST_CRASH=quit run 120 "$OUT/c-session.txt" --no-restore
rc=$?
left=$(ls "$SESSIONS"/*.json 2>/dev/null | wc -l)
[ $rc -eq 0 ] && [ "$left" -eq 0 ] && pass sessions || fail sessions "rc=$rc, markers left: $left"

# ---- add-ons: Windows packages install, a Linux package with libraries is refused
for pkg in "$ADDONS"/*-windows.qbpkg; do
    [ -f "$pkg" ] || continue
    n=$(basename "$pkg" .qbpkg)
    run 120 "$OUT/c-install-$n.txt" --install "$(win "$pkg")" --allow-unsigned
    rc=$?
    [ $rc -eq 0 ] && pass "install-$n" || fail "install-$n" "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-install-$n.txt" | head -c 300)"
done
lin="$OUT/c-linux-addon"
rm -rf "$lin" && mkdir -p "$lin/payload/plugins"
printf '{"id":"check-linux","name":"Check","version":"1.0"}' > "$lin/manifest.json"
echo "not a library" > "$lin/payload/plugins/libqb-check.so"
"$PY" "$ROOT/tools/make-package.py" addon "$(win "$lin")" "$(win "$lin/check-linux.qbpkg")" > /dev/null
run 60 "$OUT/c-install-linux.txt" --install "$(win "$lin/check-linux.qbpkg")"
rc=$?
[ $rc -ne 0 ] && grep -q "made for Linux" "$OUT/c-install-linux.txt" && pass refuse-linux-addon \
    || fail refuse-linux-addon "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-install-linux.txt" | head -c 300)"
# a writing tool's plugin (a DLL) is loaded: its command is known
run 60 "$OUT/c-tool.txt" --tool text-statistics.show
grep -q "needs the main window" "$OUT/c-tool.txt" && pass tool-plugin \
    || fail tool-plugin "$(tr '\r\n' '  ' < "$OUT/c-tool.txt" | head -c 300)"
# the media add-on starts (Qt Multimedia and Qt's Windows backend load)
QB_TEST_CRASH=media run 120 "$OUT/c-media.txt" --no-restore
grep -q "^media: started" "$OUT/c-media.txt" && pass media-start \
    || fail media-start "$(tr '\r\n' '  ' < "$OUT/c-media.txt" | head -c 400)"

# ---- the instance link (named pipes): a second scratch pad finds the first one
( cd "$DIST" && QT_QPA_PLATFORM=offscreen QB_NO_DIALOGS=1 timeout 90 "$EXE" --tool scratch-pad.show > "$OUT/c-pad1.txt" 2>&1 & )
sleep 10
markers=$(ls "$(cygpath -u "$TEMP")"/quillbinder/*.sock 2>/dev/null | wc -l)
run 40 "$OUT/c-pad2.txt" --tool scratch-pad.show
rc=$?
[ $rc -eq 0 ] && [ "$markers" -ge 1 ] && pass instance-link || fail instance-link "rc=$rc markers=$markers"
taskkill //F //IM quillbinder.exe > /dev/null 2>&1

# ---- W4b, the portable program: its settings beside it, nothing in Windows' own folders or the registry
[ -f "$DATA/settings/Quillbinder/Quillbinder.ini" ] && pass portable-settings \
    || fail portable-settings "no settings file in $DATA/settings ($(ls "$DATA" 2>/dev/null | tr '\n' ' '))"
outside=""
for d in "$(cygpath -u "$LOCALAPPDATA")/Quillbinder" "$(cygpath -u "$APPDATA")/Quillbinder" \
         "$(cygpath -u "$APPDATA")/Microsoft/Windows/Start Menu/Programs/Quillbinder.lnk" \
         "$(cygpath -u "$USERPROFILE")/Desktop/Quillbinder.lnk" \
         "$(cygpath -u "$USERPROFILE")/Downloads/Quillbinder web pages" \
         $(ls -d "$(cygpath -u "$TEMP")"/quillbinder/run-* 2>/dev/null); do # 0.90.4: Downloads, and no folder of a run left in %TEMP%
    [ -e "$d" ] && outside="$outside $d ($(ls -A "$d" 2>/dev/null | head -5 | tr '\n' ' '))"
done
for key in 'HKCU\Software\Quillbinder' 'HKCU\Software\Classes\Quillbinder.Project' 'HKCU\Software\Classes\.qbx'; do
    MSYS_NO_PATHCONV=1 reg query "$key" > /dev/null 2>&1 && outside="$outside $key"
done
[ -z "$outside" ] && pass portable-nothing-outside || fail portable-nothing-outside "$outside"
# a program folder that cannot be written is refused, and nothing is written elsewhere instead
ro="$OUT/c-readonly/Quillbinder"
rm -rf "$OUT/c-readonly" && mkdir -p "$OUT/c-readonly" && cp -r "$DIST" "$ro" && rm -rf "$ro/data"
# (only "create files" and "create folders" denied: icacls' W also takes SYNCHRONIZE, and the folder could then not even be
# the working folder of a program, W4b run 1)
MSYS_NO_PATHCONV=1 icacls "$(win "$ro")" /deny "$USERNAME:(OI)(CI)(WD,AD)" > "$OUT/c-icacls.txt" 2>&1
( cd "$ro" && QT_QPA_PLATFORM=offscreen QB_NO_DIALOGS=1 timeout 60 ./quillbinder.exe --no-restore > "$OUT/c-readonly.txt" 2>&1 )
rc=$?
MSYS_NO_PATHCONV=1 icacls "$(win "$ro")" /remove:d "$USERNAME" >> "$OUT/c-icacls.txt" 2>&1
# (Windows may let the empty folder "data" be made: nothing may be written into it)
if [ $rc -eq 2 ] && grep -q "cannot work there" "$OUT/c-readonly.txt" && [ -z "$(ls -A "$ro/data" 2>/dev/null)" ] \
   && [ ! -e "$(cygpath -u "$LOCALAPPDATA")/Quillbinder" ]; then
    pass portable-readonly-folder
else
    fail portable-readonly-folder "rc=$rc $(tr '\r\n' '  ' < "$OUT/c-readonly.txt" | head -c 300) icacls: $(tr '\r\n' '  ' < "$OUT/c-icacls.txt" | head -c 200)"
fi
rm -rf "$OUT/c-readonly"

echo "checks: $ok ok, $failed failed" | tee -a "$LOG"
[ $failed -eq 0 ]
