#!/bin/bash
# Publishes a release on the project page (0.50); run by .github/workflows/release.yml with
# GH_TOKEN set (GitHub's own token of the workflow). Releases cannot be made from a Claude Code
# session, so this runs on GitHub.
#
#   tools/publish-release.sh <tag> <dist dir> <sources dir>
#
# 1. The release "third-party-sources" (made once) gets every source file of <sources dir> it does
#    not have yet; files already there are kept, so older versions keep their sources.
# 2. The release <tag> gets the files of <dist dir> and the text of tools/release-notes.py; it is
#    marked as a pre-release while the version has a label ("alpha", "beta").
set -euo pipefail
TAG=${1:?tag}
DIST=$(cd "${2:?dist dir}" && pwd)
SRC=$(cd "${3:?sources dir}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^set(APP_VERSION *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
LABEL=$(sed -n 's/^set(APP_VERSION_LABEL *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
NAME=$(sed -n 's/^set(APP_NAME *"\(.*\)")/\1/p' "$ROOT/CMakeLists.txt")
if [ "${TAG#v}" != "$VERSION" ] && [ "${TAG#v}" != "$VERSION-$LABEL" ]; then
    echo "The tag $TAG does not fit the version $VERSION${LABEL:+ $LABEL} of CMakeLists.txt" >&2
    exit 1
fi

# ---- third-party source code
if ! gh release view third-party-sources > /dev/null 2>&1; then
    cat > "$RUNNER_TEMP/sources-notes.md" <<TEXT
The source code of every third-party library that the $NAME AppImage and its add-ons carry, each in
exactly the version used. Every release of $NAME names the files it needs here
(THIRD-PARTY-SOURCES.txt of that release, and Help > Third-Party Licenses in the program).
Files are added for new versions and never removed, so the source of older releases stays available.

Each file is a tar archive (\`tar xf <file>\`):

- \`<package>_<version>.src.tar\`: an Ubuntu 24.04 source package as \`apt-get source\` fetches it
  (the original upstream archive, Ubuntu's changes and the .dsc file)
- \`ffmpeg-6.1.1-quillbinder-<hash>.src.tar\`: FFmpeg 6.1.1 with the patch and the build scripts of
  the media add-ons
- \`appimage-type2-runtime-<commit>.src.tar\`: the AppImage runtime with the libraries it links in
TEXT
    gh release create third-party-sources --title "Third-party source code" \
        --notes-file "$RUNNER_TEMP/sources-notes.md" --latest=false --target "${GITHUB_SHA:-main}"
fi
have=$(gh release view third-party-sources --json assets --jq '.assets[].name')
for f in "$SRC"/*.src.tar; do
    n=$(basename "$f")
    if grep -qxF "$n" <<< "$have"; then
        echo "already there: $n"
    else
        echo "uploading $n"
        gh release upload third-party-sources "$f"
    fi
done

# ---- the release of this version
cp "$SRC/SOURCES.txt" "$DIST/THIRD-PARTY-SOURCES.txt"
(cd "$DIST" && rm -f SHA256SUMS.txt && sha256sum -- * > "$RUNNER_TEMP/SHA256SUMS.txt" && mv "$RUNNER_TEMP/SHA256SUMS.txt" SHA256SUMS.txt)
python3 -I "$ROOT/tools/release-notes.py" "$DIST" > "$RUNNER_TEMP/notes.md"
TITLE="$NAME $VERSION${LABEL:+ $LABEL}"
PRE=()
[ -n "$LABEL" ] && PRE=(--prerelease)
if gh release view "$TAG" > /dev/null 2>&1; then
    gh release edit "$TAG" --title "$TITLE" --notes-file "$RUNNER_TEMP/notes.md" "${PRE[@]}"
    gh release upload "$TAG" "$DIST"/* --clobber
else
    gh release create "$TAG" --title "$TITLE" --notes-file "$RUNNER_TEMP/notes.md" "${PRE[@]}" "$DIST"/*
fi
echo "published $TAG"
