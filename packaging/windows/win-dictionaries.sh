#!/usr/bin/env bash
# Spelling dictionaries of the Windows program (port, W3, 2026-11-08): English (United States, United
# Kingdom) and German (Germany, Austria, Switzerland) from LibreOffice's dictionary repository at a
# fixed tag, checked against packaging/windows/win-dictionaries.sha256. Linux uses the system's dictionaries.
#
#   packaging/windows/win-dictionaries.sh <program folder>      -> <program folder>/dictionaries/
#
# The files are unchanged; the German ones are renamed from de_XX_frami to de_XX (as Ubuntu links
# them). Their license notes go to dictionaries/licenses/ (Help > Third-Party Licenses lists them too).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TAG=libreoffice-25.8.7.3
OUT="$1/dictionaries"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
tr -d '\r' < "$ROOT/packaging/windows/win-dictionaries.sha256" > "$WORK/sums"

git -c core.autocrlf=false clone -q --depth 1 --branch "$TAG" --filter=blob:none --no-checkout https://github.com/LibreOffice/dictionaries "$WORK/lo" 2> /dev/null
(
    cd "$WORK/lo"
    git config core.autocrlf false
    # patterns without a leading slash: Git Bash would turn /en/... into C:/Program Files/Git/en/...
    MSYS_NO_PATHCONV=1 git sparse-checkout set --no-cone $(awk '{print $2}' "$WORK/sums") > /dev/null
    git -c advice.detachedHead=false checkout -q
    sha256sum -c --quiet "$WORK/sums"
)
mkdir -p "$OUT/licenses"
for l in en_US en_GB; do cp "$WORK/lo/en/$l.aff" "$WORK/lo/en/$l.dic" "$OUT/"; done
for l in de_DE de_AT de_CH; do
    cp "$WORK/lo/de/${l}_frami.aff" "$OUT/$l.aff"
    cp "$WORK/lo/de/${l}_frami.dic" "$OUT/$l.dic"
done
cp "$WORK/lo/en/README_en_US.txt" "$WORK/lo/en/README_en_GB.txt" "$OUT/licenses/"
cp "$WORK/lo/en/README.txt" "$OUT/licenses/README_en.txt"
cp "$WORK/lo/en/license.txt" "$OUT/licenses/license_en.txt"
cp "$WORK/lo/de/README_de_DE_frami.txt" "$WORK/lo/de/COPYING_GPLv2" "$WORK/lo/de/COPYING_GPLv3" "$OUT/licenses/"
cat > "$OUT/licenses/README.txt" <<TEXT
Spelling dictionaries of Quillbinder for Windows
================================================

From LibreOffice's dictionary repository (https://github.com/LibreOffice/dictionaries, tag $TAG),
unchanged; de_DE, de_AT and de_CH are the files de_DE_frami, de_AT_frami and de_CH_frami.

en_US   English (United States), from SCOWL: README_en_US.txt (a permissive license)
en_GB   English (United Kingdom): README_en_GB.txt, README_en.txt (GNU LGPL as stated there)
de_*    German (frami, based on igerman98): README_de_DE_frami.txt; GNU GPL version 2 or 3
        (COPYING_GPLv2, COPYING_GPLv3)

The files are the dictionaries in the form LibreOffice and Ubuntu distribute and maintain them. A copy
of that form is also in the release "Third-party source code" of Quillbinder.
TEXT
ls "$OUT"
