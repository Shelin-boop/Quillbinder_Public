#!/usr/bin/env bash
# The setup programs for Windows and their checks (port, step W4d, 0.90.4, 2026-11-13). Runs in Git Bash
# (packaging/windows/win-ci.sh) after the main and the full program folders are made.
#
#   packaging/windows/win-setup.sh <version> <main program folder> <full program folder> <results dir>
#
# Makes <results>/Quillbinder-<v>-windows-x64-setup.exe and <results>/Quillbinder-<v>-full-windows-x64-setup.exe with
# Inno Setup (packaging/windows/quillbinder-setup.iss), then checks what the writer asked of them (2026-11-11): no admin
# rights, only unpacking into the chosen folder, the same files as the zip, no uninstaller, nothing in the registry, no
# Start menu or desktop entry, "data" of an older folder kept, a folder that cannot be written refused.
# Lines "setup: ...", "check <name>: ok|FAIL <why>", then "checks: ...".
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=$1
MAIN=$(cd "$2" && pwd)
FULL=$(cd "$3" && pwd)
OUT=$(cd "$4" && pwd)
LABEL=$(sed -n 's/^set(APP_VERSION_LABEL "\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
ok=0
failed=0
pass() { echo "check $1: ok"; ok=$((ok + 1)); }
fail() { echo "check $1: FAIL $2"; failed=$((failed + 1)); }
finish() {
    echo "checks: $ok ok, $failed failed"
    # the small output files in one archive (setup-run.log, this script's own output, stays beside it)
    ( cd "$OUT" && files=$(ls setup-*.log setup-*.txt 2>/dev/null | grep -v '^setup-run\.log$') && [ -n "$files" ] \
        && 7z a -tzip setup-files.zip $files > /dev/null && rm -f $files )
    [ $failed -eq 0 ]
    exit $?
}

# ---- Inno Setup's compiler: on the runner's image, else installed with Chocolatey
find_iscc() {
    local c
    for c in "$(command -v ISCC.exe 2>/dev/null)" "$(command -v iscc 2>/dev/null)" \
             "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe" \
             "/c/Program Files (x86)/Inno Setup 7/ISCC.exe" "/c/Program Files/Inno Setup 7/ISCC.exe"; do
        [ -n "$c" ] && [ -f "$c" ] && { echo "$c"; return 0; }
    done
    return 1
}
ISCC=$(find_iscc)
if [ -z "$ISCC" ]; then
    echo "setup: Inno Setup is not on this machine, installing it with Chocolatey"
    choco install innosetup -y --no-progress > "$OUT/setup-choco.log" 2>&1 || echo "setup: choco rc=$?"
    ISCC=$(find_iscc)
fi
if [ -z "$ISCC" ]; then
    echo "setup: no Inno Setup compiler (ISCC.exe) found"
    fail setup-compiler "ISCC.exe not found; $(tail -3 "$OUT/setup-choco.log" 2>/dev/null | tr '\r\n' '  ' | head -c 300)"
    finish
fi
echo "setup: compiler $ISCC ($(MSYS_NO_PATHCONV=1 "$ISCC" '/?' 2>&1 | tr -d '\r' | grep -m1 -i 'inno setup' | head -c 120))"

# the script with a byte order mark: without it the compiler reads the German texts in the machine's code page
WORK="$OUT/setup-work"
rm -rf "$WORK" && mkdir -p "$WORK"
ISS="$WORK/quillbinder-setup.iss"
if [ "$(head -c 3 "$ROOT/packaging/windows/quillbinder-setup.iss" | od -An -tx1 | tr -d ' \n')" = "efbbbf" ]; then
    cp "$ROOT/packaging/windows/quillbinder-setup.iss" "$ISS"
else
    { printf '\xef\xbb\xbf'; cat "$ROOT/packaging/windows/quillbinder-setup.iss"; } > "$ISS"
fi

compile() { # <program folder> <file name without .exe>
    MSYS_NO_PATHCONV=1 "$ISCC" /Q "/DAppVer=$VER" "/DAppLabel=$LABEL" "/DSourceDir=$(cygpath -w "$1")" \
        "/DOutDir=$(cygpath -w "$OUT")" "/DOutName=$2" "/DIconFile=$(cygpath -w "$ROOT/resources/app/quillbinder.ico")" \
        "$(cygpath -w "$ISS")" > "$OUT/setup-compile-$2.log" 2>&1
}
[ -e "$MAIN/data" ] && echo "setup: the main folder has a data folder (left out by the script)"
MAIN_EXE="Quillbinder-$VER-windows-x64-setup"
FULL_EXE="Quillbinder-$VER-full-windows-x64-setup"
compile "$MAIN" "$MAIN_EXE"; mrc=$?
compile "$FULL" "$FULL_EXE"; frc=$?
for pair in "main:$MAIN_EXE:$mrc" "full:$FULL_EXE:$frc"; do
    kind=${pair%%:*}; rest=${pair#*:}; name=${rest%%:*}; rc=${rest##*:}
    size=$(du -k "$OUT/$name.exe" 2>/dev/null | cut -f1)
    echo "setup: $kind rc=$rc ${size:-0} KiB"
    if [ "$rc" -eq 0 ] && [ "${size:-0}" -gt 10000 ]; then
        pass "setup-$kind-built"
    else
        fail "setup-$kind-built" "rc=$rc size=${size:-0} KiB: $(tr -d '\r' < "$OUT/setup-compile-$name.log" | grep -iE 'error|line' | head -5 | tr '\n' ' ' | head -c 500)"
    fi
done
[ -f "$OUT/$MAIN_EXE.exe" ] || finish

# ---- what is on the machine before: the "Apps" entries, the Start menus, the desktops
snapshot() { # <file>
    {
        for key in 'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall' \
                   'HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall' \
                   'HKLM\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall' \
                   'HKCU\Software\Classes' 'HKCU\Software\Quillbinder' 'HKCU\Software\The Quillbinder project'; do
            MSYS_NO_PATHCONV=1 reg query "$key" /s /f Quillbinder 2>/dev/null | tr -d '\r' | grep -i 'quillbinder'
        done
        for dir in "$APPDATA/Microsoft/Windows/Start Menu" "$PROGRAMDATA/Microsoft/Windows/Start Menu" \
                   "$USERPROFILE/Desktop" "$PUBLIC/Desktop" "$LOCALAPPDATA/Quillbinder" "$APPDATA/Quillbinder" \
                   "$LOCALAPPDATA/Programs"; do
            [ -d "$dir" ] && find "$dir" -iname '*quillbinder*' 2>/dev/null
        done
    } | sort > "$1"
}
tree() { ( cd "$1" && find . -type f ! -path './data/*' | LC_ALL=C sort | while IFS= read -r f; do echo "$(sha256sum < "$f" | cut -d' ' -f1) $f"; done ); }
unpack() { # <setup exe> <folder> <log>: runs the setup without its pages, as the present user
    MSYS_NO_PATHCONV=1 timeout 600 "$1" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LANG=english "/DIR=$(cygpath -w "$2")" \
        "/LOG=$(cygpath -w "$3")"
}
snapshot "$WORK/before.txt"

# ---- main: unpacked, the same as the zip's folder
T="$WORK/Unpacked Main äöü"
unpack "$OUT/$MAIN_EXE.exe" "$T" "$OUT/setup-main-run.log"; rc=$?
if [ $rc -eq 0 ] && [ -f "$T/quillbinder.exe" ]; then pass setup-main-unpacks; else fail setup-main-unpacks "rc=$rc $(tail -5 "$OUT/setup-main-run.log" 2>/dev/null | tr '\r\n' '  ' | head -c 400)"; fi
tree "$MAIN" > "$WORK/main-zip.txt"
tree "$T" > "$WORK/main-setup.txt"
if [ -s "$WORK/main-zip.txt" ] && cmp -s "$WORK/main-zip.txt" "$WORK/main-setup.txt"; then
    pass setup-main-same-as-zip
else
    fail setup-main-same-as-zip "$(wc -l < "$WORK/main-zip.txt") files in the zip's folder, $(wc -l < "$WORK/main-setup.txt") unpacked; first differences: $(diff "$WORK/main-zip.txt" "$WORK/main-setup.txt" | grep '^[<>]' | head -6 | cut -c1,66- | tr '\n' ' ' | head -c 400)"
fi
unins=$(find "$T" -maxdepth 1 -iname 'unins*' | wc -l)
[ "$unins" -eq 0 ] && pass setup-no-uninstaller || fail setup-no-uninstaller "$unins file(s): $(find "$T" -maxdepth 1 -iname 'unins*' | head -3 | tr '\n' ' ')"

# ---- the unpacked program starts (its self-test, without a window), and keeps its data beside itself
( cd "$T" && QT_QPA_PLATFORM=offscreen QB_NO_DIALOGS=1 timeout 180 ./quillbinder.exe --selftest > "$OUT/setup-selftest.txt" 2>&1 ); rc=$?
if [ $rc -eq 0 ] && grep -q 'selftest ok' "$OUT/setup-selftest.txt"; then pass setup-main-starts; else fail setup-main-starts "rc=$rc $(tr '\r\n' '  ' < "$OUT/setup-selftest.txt" | head -c 300)"; fi

# ---- a second run over the folder keeps what the writer has in "data"
mkdir -p "$T/data/settings" && echo "kept" > "$T/data/settings/setup-check-marker.txt"
unpack "$OUT/$MAIN_EXE.exe" "$T" "$OUT/setup-main-run2.log"; rc=$?
if [ $rc -eq 0 ] && [ "$(cat "$T/data/settings/setup-check-marker.txt" 2>/dev/null)" = "kept" ] && [ -f "$T/quillbinder.exe" ]; then
    pass setup-keeps-data
else
    fail setup-keeps-data "rc=$rc marker: $(cat "$T/data/settings/setup-check-marker.txt" 2>/dev/null || echo gone)"
fi

# ---- full: unpacked with its add-ons, the same as the full zip's folder
TF="$WORK/Unpacked Full"
unpack "$OUT/$FULL_EXE.exe" "$TF" "$OUT/setup-full-run.log"; rc=$?
tree "$FULL" > "$WORK/full-zip.txt"
tree "$TF" > "$WORK/full-setup.txt" 2>/dev/null
n=$(ls "$TF/full-addons"/*.qbpkg 2>/dev/null | wc -l)
if [ $rc -eq 0 ] && [ "$n" -gt 0 ] && cmp -s "$WORK/full-zip.txt" "$WORK/full-setup.txt"; then
    pass setup-full-same-as-zip
else
    fail setup-full-same-as-zip "rc=$rc, $n add-on packages, $(wc -l < "$WORK/full-zip.txt") files in the zip's folder, $(wc -l < "$WORK/full-setup.txt") unpacked"
fi

# ---- a folder that cannot be written (as Program Files for a user without admin rights): refused, nothing unpacked
RO="$WORK/No Writing"
mkdir -p "$RO"
MSYS_NO_PATHCONV=1 icacls "$(cygpath -w "$RO")" /deny "*S-1-1-0:(OI)(CI)(WD,AD)" > "$OUT/setup-icacls.txt" 2>&1
unpack "$OUT/$MAIN_EXE.exe" "$RO/Quillbinder" "$OUT/setup-readonly-run.log"; rc=$?
MSYS_NO_PATHCONV=1 icacls "$(cygpath -w "$RO")" /remove:d "*S-1-1-0" >> "$OUT/setup-icacls.txt" 2>&1
left=$(find "$RO" -type f 2>/dev/null | wc -l)
if [ $rc -ne 0 ] && [ "$left" -eq 0 ]; then
    pass setup-refuses-folder-without-rights
else
    fail setup-refuses-folder-without-rights "rc=$rc, $left file(s) unpacked; $(tail -4 "$OUT/setup-readonly-run.log" 2>/dev/null | tr '\r\n' '  ' | head -c 300)"
fi

# ---- nothing else changed on the machine: no "Apps" entry, no registry key, no Start menu or desktop entry
snapshot "$WORK/after.txt"
if cmp -s "$WORK/before.txt" "$WORK/after.txt"; then
    pass setup-nothing-outside
else
    fail setup-nothing-outside "$(diff "$WORK/before.txt" "$WORK/after.txt" | grep '^[<>]' | head -6 | tr '\n' ' ' | head -c 500)"
fi
# the setup asks for no admin rights: its manifest says "asInvoker" (a setup that needs them says "requireAdministrator")
if grep -a -q 'asInvoker' "$OUT/$MAIN_EXE.exe" && ! grep -a -q 'requireAdministrator' "$OUT/$MAIN_EXE.exe"; then
    pass setup-no-admin-rights
else
    fail setup-no-admin-rights "the manifest: $(grep -a -o 'requestedExecutionLevel[^/]*' "$OUT/$MAIN_EXE.exe" | head -2 | tr '\n' ' ')"
fi

rm -rf "$WORK"
finish
