#!/bin/bash
# Makes the public repository's files equal to the private repository's committed source (0.60).
#
#   tools/publish-public.sh <clone of Quillbinder_Public> [--commit]
#
# 1. Exports HEAD of this repository (committed files only, never the history) without the paths in
#    tools/public-exclude.txt, and takes the blocks between "<!-- private:begin -->" and
#    "<!-- private:end -->" out of the text files.
# 2. Checks the export with tools/legal-check.py --public and stops when a check fails.
# 3. Makes the public clone's files equal to the export (its .git and its own history stay) and
#    shows what changed.
# 4. With --commit: commits there as "Quillbinder <version> <label>" (or QB_COMMIT_SUBJECT for a
#    change within a version), naming the private commit.
# Before all this it checks that the download links at the top of README.md name this version.
#
# Pushing is a separate step, done only when the user says so:
#    git -C <clone of Quillbinder_Public> push origin main
# A new version on the public main starts .github/workflows/release.yml there, which builds and
# publishes the release (AppImage, program update, add-ons, the media package with all formats).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PUB=${1:?usage: tools/publish-public.sh <clone of Quillbinder_Public> [--commit]}
COMMIT=${2:-}
PUB=$(cd "$PUB" && pwd)
[ -d "$PUB/.git" ] || { echo "$PUB is not a git clone" >&2; exit 2; }
[ "$PUB" != "$ROOT" ] || { echo "the public clone must be another folder" >&2; exit 2; }
cd "$ROOT"
if ! git diff --quiet HEAD -- || [ -n "$(git ls-files --others --exclude-standard | head -1)" ]; then
    echo "Commit or remove the changes first: only committed files are published." >&2
    git status --short | head -20 >&2
    exit 2
fi
SHA=$(git rev-parse --short=12 HEAD)
VERSION=$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' CMakeLists.txt)
LABEL=$(sed -n 's/^set(APP_VERSION_LABEL *"\(.*\)")/\1/p' CMakeLists.txt)
# the download links at the top of the README must name this version (0.60)
TAG="v$VERSION${LABEL:+-$LABEL}"
for f in "Quillbinder-$VERSION${LABEL:+-$LABEL}-x86_64.AppImage" "Quillbinder-$VERSION-full${LABEL:+-$LABEL}-x86_64.AppImage" \
         media-all-formats.qbpkg; do
    if ! grep -qF "releases/download/$TAG/$f)" README.md; then
        echo "README.md: the download links must point to $TAG (releases/download/$TAG/$f)." >&2
        exit 2
    fi
done
EXPORT=$(mktemp -d)
trap 'rm -rf "$EXPORT"' EXIT

# 1. export
git archive HEAD | tar -x -C "$EXPORT"
while IFS= read -r line; do
    line=${line%%#*}; line=$(echo "$line" | xargs)
    [ -z "$line" ] && continue
    case "$line" in /*|*..*) echo "bad path in tools/public-exclude.txt: $line" >&2; exit 2 ;; esac
    rm -rf "${EXPORT:?}/$line"
done < tools/public-exclude.txt
python3 -I - "$EXPORT" <<'PY'
import os, re, sys
root = sys.argv[1]
block = re.compile(r"^[ \t]*<!-- private:begin -->.*?^[ \t]*<!-- private:end -->[ \t]*\n", re.S | re.M)
for d, dirs, files in os.walk(root):
    for f in files:
        if not f.endswith((".md", ".txt")):
            continue
        p = os.path.join(d, f)
        with open(p, encoding="utf-8", errors="surrogateescape") as fh:
            t = fh.read()
        marker = re.compile(r"^[ \t]*<!-- private:(begin|end) -->", re.M)
        if not marker.search(t):
            continue
        n = block.sub("", t)
        if marker.search(n):
            sys.exit("an unclosed private block in %s" % os.path.relpath(p, root))
        with open(p, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(n)
        print("private blocks taken out of %s" % os.path.relpath(p, root))
PY

# 2. check
echo "== legal check of the public copy"
python3 -I "$ROOT/tools/legal-check.py" --public "$EXPORT"

# 3. the public clone's files = the export
echo "== into $PUB"
find "$PUB" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -a "$EXPORT/." "$PUB/"
git -C "$PUB" add -A
CHANGED=$(git -C "$PUB" diff --cached --name-only | wc -l)
git -C "$PUB" diff --cached --stat | tail -1
echo "$CHANGED file(s) changed"

# 4. commit (no push)
if [ "$COMMIT" = --commit ] && [ "$CHANGED" -gt 0 ]; then
    git -C "$PUB" -c user.name="${GIT_AUTHOR_NAME:-Claude}" -c user.email="${GIT_AUTHOR_EMAIL:-noreply@anthropic.com}" \
        commit -q -m "${QB_COMMIT_SUBJECT:-Quillbinder $VERSION${LABEL:+ $LABEL}}" -m "Source of the development repository, commit $SHA (tools/publish-public.sh)." \
        ${QB_COMMIT_NOTE:+-m "$QB_COMMIT_NOTE"}
    git -C "$PUB" log --oneline -1
    echo "Committed, not pushed. To publish: git -C $PUB push origin main"
fi
