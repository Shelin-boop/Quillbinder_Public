#!/bin/bash
# Backward compatibility of the objects (0.20): a project with objects, made by this version,
# is opened in an OLDER AppImage, gets a new document there and is saved and closed; then this
# version must still find every object, link and picture, and the older version must have left
# the folder objects/ untouched.
#
#   tools/e2e-old-version.sh <older AppImage> [build dir] [work dir]
#
# Needs: a build with tests (build/tests/qb_v020_tests), Xvfb, xdotool, python3. The older
# AppImage runs with a home folder of its own (so it starts its own program, not an installed
# update). Uses the instance link of the program (as tools/e2e-appimage.sh does).
set -u
OLD=$(readlink -f "${1:?older AppImage}")
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$(readlink -f "${2:-$ROOT/build}")
E=$(mkdir -p "${3:-$ROOT/build/e2e-old}" && cd "${3:-$ROOT/build/e2e-old}" && pwd)
chmod +x "$OLD"
rm -rf "$E/home" "$E/work" "$E/shots"
mkdir -p "$E/home" "$E/work" "$E/shots"
# the runtime folder holds the program's link socket: a short path (sockets allow 107 characters)
RUN=$(mktemp -d /tmp/qbrun.XXXXXX)
chmod 700 "$RUN"
fails=0
expect() { if eval "$1"; then echo "ok    $2"; else echo "FAIL  $2"; fails=$((fails + 1)); fi; }

echo "== a project with objects, made by this version"
P=$(HOME=$E/home QT_QPA_PLATFORM=offscreen "$B/tests/qb_v020_tests" --make-fixture "$E/work" | tail -1)
echo "$P"
expect '[ -d "$P/objects" ]' "the project has an objects folder"
( cd "$P" && find objects -type f -print0 | sort -z | xargs -0 sha1sum ) > "$E/work/objects-before.sha"

export HOME=$E/home XDG_CONFIG_HOME=$E/home/.config XDG_DATA_HOME=$E/home/.local/share XDG_RUNTIME_DIR=$RUN
unset QT_QPA_PLATFORM
OLDVER=$("$OLD" --appimage-extract-and-run --version | awk '{print $NF}')
echo "== opened in the older version $OLDVER"
D=87; while [ -e /tmp/.X11-unix/X$D ] || [ -e /tmp/.X$D-lock ]; do D=$((D + 1)); done
Xvfb :$D -screen 0 1600x1000x24 > /dev/null 2>&1 &
XPID=$!
sleep 2
export DISPLAY=:$D
QBX=$(ls "$P"/*.qbx | head -1)
setsid "$OLD" --appimage-extract-and-run --no-restore "$QBX" > "$E/work/app-old.log" 2>&1 &
SID=$!
W=""
for i in $(seq 1 60); do
    # first start of an AppImage: "Application Menu" question (No = Escape)
    for d in $(xdotool search --onlyvisible --name "Application Menu" 2>/dev/null); do
        xdotool windowactivate --sync "$d" 2>/dev/null; sleep 0.5; xdotool key Escape; sleep 1
    done
    W=$(xdotool search --onlyvisible --name "Objects Fixture" 2>/dev/null | head -1)
    [ -n "$W" ] && break
    sleep 1
done
sleep 2
import -window root "$E/shots/1-old-version.png"
expect '[ -n "$W" ]' "the older version opens the project (\"$(xdotool getwindowname "$W" 2>/dev/null)\")"
SOCK=$(ls -t "$XDG_RUNTIME_DIR"/quillbinder/*.sock 2>/dev/null | head -1)
printf 'Written in the older version.\n' > "$E/work/note.txt"
python3 - "$SOCK" "$E/work" <<'PY'
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
draft = next(n for n in info["binder"] if n.get("folder"))
r = q({"cmd": "send", "request": {"file": work + "/note.txt", "title": "E2E Note", "mode": "import", "targetId": draft["id"]}})
print("send new document:", r.get("ok"))
PY
sleep 2
xdotool windowactivate --sync "$W" 2>/dev/null; sleep 0.5; xdotool key ctrl+s; sleep 2
import -window root "$E/shots/2-old-version-saved.png"
xdotool windowactivate --sync "$W" 2>/dev/null; sleep 0.5; xdotool key ctrl+q; sleep 4
pkill -TERM -s "$SID" 2>/dev/null; sleep 2; pkill -KILL -s "$SID" 2>/dev/null
kill "$XPID" 2>/dev/null
rm -rf "$RUN"
expect 'grep -q "E2E Note" "$QBX"' "the older version saved the project (the new document is in the project file)"
( cd "$P" && find objects -type f -print0 | sort -z | xargs -0 sha1sum ) > "$E/work/objects-after.sha"
expect 'cmp -s "$E/work/objects-before.sha" "$E/work/objects-after.sha"' "the older version left every file of objects/ as it was"

echo "== this version again"
unset DISPLAY
HOME=$E/home QT_QPA_PLATFORM=offscreen "$B/tests/qb_v020_tests" --check-fixture "$P" | tee "$E/work/check.log"
expect 'grep -q "^0 failure(s)" "$E/work/check.log"' "this version finds the objects, their links and pictures"
echo
echo "$fails failure(s)"
exit $fails
