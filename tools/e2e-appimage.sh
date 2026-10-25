#!/bin/bash
# End-to-end check of a program update with the real AppImage, the way the user
# installs it: install the update package, self-test, a cut-off package must be
# refused, the window must come up (at the first start with the welcome project, whose
# first document is printed into a PDF file with the AppImage's own libraries), and (with
# a Scrivener sample) a .scriv project opened in Scrivener format must keep its binder
# order and dates.
#
#   tools/e2e-appimage.sh <AppImage> <update.qbpkg> [work dir]
#   tools/e2e-appimage.sh <AppImage> - [work dir]     the AppImage alone (a new AppImage)
#
#   QB_PREV_UPDATE=<older update.qbpkg>   installed first; at the end the program is
#                                         switched back to it and must still start
#   QB_SCRIV_SAMPLE=<folder.scriv>        a Scrivener 3 project for the GUI part
#
# Needs: Xvfb, xdotool, ImageMagick (import), poppler-utils, python3. No FUSE: the AppImage runs with
# --appimage-extract-and-run. Screenshots and logs go into the work dir (default
# build/e2e). The base AppImage of the user is on the branch "assets" of the repository
# (Quillbinder-0.30.0-alpha-x86_64.AppImage since 2026-10-25; before: 0.20.0, 0.16.0 and 0.2.0, the
# same libraries):
# git fetch origin assets:refs/remotes/origin/assets, then
# git show origin/assets:Quillbinder-0.30.0-alpha-x86_64.AppImage > /tmp/qb.AppImage
# The work dir needs a short path: the program's instance socket lies in <work dir>/run/quillbinder/,
# and a socket path may have at most 107 bytes (a longer one: the Scrivener part cannot reach the program).
#
# Every program start runs in its own session (setsid), and stop_app ends exactly that
# session: no process is looked up by name, so nothing else on the machine is hit.
set -u
APPIMAGE=$(readlink -f "${1:?AppImage}")
UPDATE=${2:?update package, or - for the AppImage alone}
[ "$UPDATE" != "-" ] && UPDATE=$(readlink -f "$UPDATE")
ROOT=$(cd "$(dirname "$0")/.." && pwd)
E=$(mkdir -p "${3:-$ROOT/build/e2e}" && cd "${3:-$ROOT/build/e2e}" && pwd)
chmod +x "$APPIMAGE"
if [ ${#E} -gt 70 ]; then
    echo "The work dir path is too long (${#E} characters, at most 70): the instance socket in it would"
    echo "exceed the 107-byte limit of socket paths. Give a shorter work dir as the third argument."; exit 2
fi
rm -rf "$E/home" "$E/run" "$E/work" "$E/shots"
mkdir -p "$E/home" "$E/run" "$E/work" "$E/shots"
chmod 700 "$E/run"
export HOME=$E/home XDG_CONFIG_HOME=$E/home/.config XDG_DATA_HOME=$E/home/.local/share XDG_RUNTIME_DIR=$E/run
unset QT_QPA_PLATFORM
cli() { "$APPIMAGE" --appimage-extract-and-run "$@"; }
ver() { cli --version | awk '{print $NF}'; }   # "Quillbinder 0.14.1" -> 0.14.1
step() { echo; echo "== $*"; }
SID=
# starts the program window in a session of its own; log in $1, arguments after it
start_app() {
    local log=$1; shift
    setsid "$APPIMAGE" --appimage-extract-and-run "$@" > "$log" 2>&1 &
    SID=$!
}
stop_app() {
    [ -z "$SID" ] && return
    pkill -TERM -s "$SID" 2>/dev/null; sleep 2
    pgrep -s "$SID" > /dev/null 2>&1 && { echo "(a process of the run was still there, killed)"; pkill -KILL -s "$SID" 2>/dev/null; }
    SID=
    # a killed program leaves its instance socket behind (the run has a runtime folder of its own)
    rm -f "$XDG_RUNTIME_DIR"/quillbinder/*.sock
}
fails=0
expect() { if eval "$1"; then echo "ok    $2"; else echo "FAIL  $2"; fails=$((fails + 1)); fi; }

step "built in"; BUILTIN=$(ver); echo "$BUILTIN"
if [ -n "${QB_PREV_UPDATE:-}" ]; then
    step "install the previous update"; cli --install "$(readlink -f "$QB_PREV_UPDATE")" | tail -2
    PREV=$(ver); echo "$PREV"
fi
if [ "$UPDATE" != "-" ]; then
    step "install the update"; cli --install "$UPDATE" | tail -2
    NEW=$(ver); echo "$NEW"; cli --versions
    expect '[ "$NEW" != "$BUILTIN" ]' "the update is the active version"
    step "self-test"; QT_QPA_PLATFORM=offscreen cli --selftest > "$E/work/selftest.log" 2>&1; rc=$?
    expect '[ $rc = 0 ]' "self-test (rc=$rc)"
    step "cut-off package"; head -c 300000 "$UPDATE" > "$E/work/cut.qbpkg"; cli --install "$E/work/cut.qbpkg"; rc=$?
    expect '[ $rc != 0 ] && [ "$(ver)" = "$NEW" ]' "a cut-off package is refused and changes nothing"
else
    NEW=$BUILTIN
    step "the AppImage alone"; cli --versions
    QT_QPA_PLATFORM=offscreen cli --selftest > "$E/work/selftest.log" 2>&1; rc=$?
    expect '[ $rc = 0 ]' "self-test of the built-in program (rc=$rc)"
fi

# a free display for the windows
D=77; while [ -e /tmp/.X11-unix/X$D ] || [ -e /tmp/.X$D-lock ]; do D=$((D + 1)); done
Xvfb :$D -screen 0 1600x1000x24 > /dev/null 2>&1 &
XPID=$!
sleep 2
export DISPLAY=:$D
# questions at the start, answered with key presses to the focused window: "Open Scrivener
# Project" (Work in Scrivener Format = Return; Scrivener's user.lock: Yes = Return),
# "Application Menu" (first start of the AppImage: No = Escape)
answer_dialogs() {
    local want=$1 n=0 shot=1 pressed=" "
    for i in $(seq 1 60); do
        local d=""
        for w in $(xdotool search --onlyvisible --name "Open Scrivener Project|Application Menu" 2>/dev/null); do
            case "$pressed" in *" $w "*) ;; *) d=$w ;; esac
        done
        if [ -n "$d" ]; then
            pressed="$pressed$d "
            local t=$(xdotool getwindowname "$d")
            import -window root "$E/shots/dialog-$shot.png"; shot=$((shot + 1))
            echo "dialog: $t"
            xdotool windowactivate --sync "$d" 2>/dev/null; sleep 0.5
            case "$t" in *"Application Menu"*) xdotool key Escape ;; *) xdotool key Return ;; esac
            sleep 1.5; n=$((n + 1)); continue
        fi
        xdotool search --onlyvisible --name "$want" > /dev/null 2>&1 && { sleep 2; return; }
        sleep 1
    done
}

# keys go to the window that has the keyboard: the main window, the print window
keys_to() {  # <window name> <keys ...>
    local w; w=$(timeout 30 xdotool search --sync --onlyvisible --name "$1" | head -1)
    [ -z "$w" ] && { echo "(no window \"$1\")"; return 1; }
    xdotool windowactivate --sync "$w" 2>/dev/null || xdotool windowfocus --sync "$w" 2>/dev/null
    sleep 0.7; shift; xdotool key --delay 150 "$@"; sleep 1.5
}

step "GUI: the first start opens the welcome project; its first document printed into a PDF file"
mkdir -p "$E/home/Documents"
rm -f "$E/home"/*.pdf
start_app "$E/work/app-start.log" --no-restore
answer_dialogs "Welcome to Quillbinder"
W=$(timeout 60 xdotool search --sync --onlyvisible --name "Quillbinder" | head -1)
sleep 3
import -window root "$E/shots/1-start.png"
expect '[ -n "$W" ]' "a window \"$(xdotool getwindowname "$W" 2>/dev/null)\""
WP=$(ls -d "$E/home/Documents/Welcome to Quillbinder".* 2>/dev/null | head -1)
expect 'xdotool getwindowname "$W" 2>/dev/null | grep -q "Welcome to Quillbinder" && [ -n "$WP" ]' \
    "the welcome project is made in Documents and opened (${WP:-no folder})"
keys_to "Welcome to Quillbinder" ctrl+p           # File > Print Current Document
import -window root "$E/shots/2-print-window.png"
keys_to "Print" Return                            # no printer here: Print to File (PDF)
sleep 4
PDF=$(find "$E/home" "$PWD" -maxdepth 3 -name "*.pdf" -newer "$E/work/app-start.log" 2>/dev/null | head -1)
echo "printed: ${PDF:-nothing}"
expect '[ -n "$PDF" ] && pdftotext "$PDF" - | grep -q "This small project shows what Quillbinder"' "\"Welcome\" is printed into a PDF file"
[ -n "$PDF" ] && pdftoppm -r 50 -png -f 1 -l 1 "$PDF" "$E/shots/3-printed" && rm -f "$PDF"
keys_to "Welcome to Quillbinder" ctrl+q
stop_app

step "GUI: the second start does not make the welcome project again"
start_app "$E/work/app-start2.log" --no-restore
answer_dialogs "Projects"
W=$(timeout 60 xdotool search --sync --onlyvisible --name "Projects|Quillbinder" | head -1)
sleep 2
import -window root "$E/shots/4-second-start.png"
expect '[ -n "$W" ] && ! xdotool search --onlyvisible --name "Welcome to Quillbinder" > /dev/null 2>&1' \
    "the start window, not the welcome project (\"$(xdotool getwindowname "$W" 2>/dev/null)\")"
stop_app

if [ -n "${QB_SCRIV_SAMPLE:-}" ]; then
    step "GUI: a Scrivener 3 project in Scrivener format"
    cp -r "$QB_SCRIV_SAMPLE" "$E/work/Sample.scriv"
    X=$(ls "$E/work/Sample.scriv"/*.scrivx | head -1)
    [ "$X" != "$E/work/Sample.scriv/Sample.scrivx" ] && mv "$X" "$E/work/Sample.scriv/Sample.scrivx"
    ORIG=$(ls "$QB_SCRIV_SAMPLE"/*.scrivx | head -1)
    ( cd "$E/work/Sample.scriv" && find . -type f -print0 | sort -z | xargs -0 sha1sum ) > "$E/work/before.sha"
    start_app "$E/work/app-scriv.log" --no-restore "$E/work/Sample.scriv/Sample.scrivx"
    answer_dialogs "Scrivener format"
    W=$(timeout 60 xdotool search --sync --onlyvisible --name "Scrivener format" | head -1)
    sleep 2
    import -window root "$E/shots/2-scrivener.png"
    expect '[ -n "$W" ]' "the project opens in Scrivener format"
    # through the instance link: a new document in the Draft, text added to an existing one
    SOCK=$(ls -t "$XDG_RUNTIME_DIR"/quillbinder/*.sock 2>/dev/null | head -1)
    printf 'A line written by the end-to-end run.\n' > "$E/work/note.txt"
    printf 'A line added at the end by the end-to-end run.\n' > "$E/work/append.txt"
    python3 - "$SOCK" "$E/work" <<'EOF'
import json, socket, sys
sock, work = sys.argv[1], sys.argv[2]
def q(o):
    s = socket.socket(socket.AF_UNIX); s.connect(sock); s.sendall((json.dumps(o) + "\n").encode())
    data = b""
    while not data.endswith(b"\n"):
        c = s.recv(65536)
        if not c: break
        data += c
    s.close(); return json.loads(data)
info = q({"cmd": "info"})
def walk(n, out):
    out.append(n); [walk(c, out) for c in n.get("children", [])]
    return out
draft = next(n for n in info["binder"] if n.get("folder"))
texts = [n for n in walk(draft, []) if n is not draft and n.get("text") and not n.get("children")]
r1 = q({"cmd": "send", "request": {"file": work + "/note.txt", "title": "E2E Note", "mode": "import", "targetId": draft["id"]}})
r2 = q({"cmd": "send", "request": {"file": work + "/append.txt", "mode": "append", "targetId": texts[0]["id"]}})
print("send new document:", r1.get("ok"), "| append to", texts[0].get("title") + ":", r2.get("ok"))
open(work + "/appended-id", "w").write(texts[0]["id"])
EOF
    sleep 2
    xdotool windowactivate --sync "$W" 2>/dev/null; sleep 0.5; xdotool key ctrl+s; sleep 2
    import -window root "$E/shots/3-after-send.png"
    xdotool windowactivate --sync "$W" 2>/dev/null; sleep 0.5; xdotool key ctrl+q; sleep 3
    stop_app
    expect 'grep -q "E2E Note" "$E/work/Sample.scriv/Sample.scrivx"' "the new document is in the .scrivx"
    expect 'grep -lq "added at the end by the end-to-end run" "$E/work/Sample.scriv"/Files/Data/*/content.rtf' \
        "the added text is in the document's content.rtf"
    python3 - "$ORIG" "$E/work/Sample.scriv/Sample.scrivx" "$(cat "$E/work/appended-id" 2>/dev/null)" > "$E/work/order.txt" <<'EOF'
import sys, xml.etree.ElementTree as ET
a = ET.parse(sys.argv[1]).getroot(); b = ET.parse(sys.argv[2]).getroot(); appended = sys.argv[3].upper()
def items(r): return [(e.get("UUID"), e) for e in r.find("Binder").iter("BinderItem")]
ia, ib = items(a), items(b)
oa = [u for u, _ in ia]; ob = [u for u, _ in ib if u in set(oa)]
da = {u: (e.get("Created"), e.get("Modified")) for u, e in ia}
changed = [u[:8] for u, e in ib if u in da and u != appended and da[u] != (e.get("Created"), e.get("Modified"))]
print("order", oa == ob, "dates", not changed)
EOF
    expect 'grep -q "order True" "$E/work/order.txt"' "the binder order of the .scrivx is kept"
    expect 'grep -q "dates True" "$E/work/order.txt"' "untouched documents keep their dates"
fi

if [ -n "${QB_PREV_UPDATE:-}" ]; then
    step "back to the previous version"
    python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['active']=sys.argv[2];d['attempts']=0;json.dump(d,open(p,'w'))" \
        "$XDG_DATA_HOME/Quillbinder/core/state.json" "$PREV"
    expect '[ "$(ver)" = "$PREV" ]' "the previous version is active again"
    start_app "$E/work/app-prev.log" --no-restore
    answer_dialogs "Quillbinder"
    W=$(timeout 60 xdotool search --sync --onlyvisible --name "Quillbinder" | head -1)
    sleep 2
    import -window root "$E/shots/4-previous.png"
    expect '[ -n "$W" ]' "the previous version starts"
    stop_app
    python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d['active']=sys.argv[2];d['attempts']=0;json.dump(d,open(p,'w'))" \
        "$XDG_DATA_HOME/Quillbinder/core/state.json" "$NEW"
fi
kill "$XPID" 2>/dev/null
echo
echo "$fails failure(s)"
exit $fails
