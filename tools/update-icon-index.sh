#!/bin/bash
# Rewrites index.theme of the bundled icon sets after icons were added,
# then regenerates resources.qrc.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$ROOT/resources/icons
for t in qbicons qbicons-dark; do
    dirs=$(cd "$S/$t" && find . -mindepth 2 -maxdepth 2 -type d | sed 's|^\./||' | sort)
    {
        echo "[Icon Theme]"; echo "Name=$t"
        echo "Comment=Subset of the KDE Breeze icons (LGPL-3.0-or-later) bundled as a fallback"
        echo "Inherits=hicolor"
        echo "Directories=$(echo $dirs | tr ' ' ',')"; echo
        for d in $dirs; do
            ctx=${d%%/*}
            echo "[$d]"; echo "Size=${d##*/}"; echo "Context=${ctx^}"; echo "Type=Fixed"; echo
        done
    } > "$S/$t/index.theme"
done
python3 "$ROOT/tools/gen-resources.py"
