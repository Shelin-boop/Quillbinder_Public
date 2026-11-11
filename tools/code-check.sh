#!/usr/bin/env bash
# 0.81.0 (audit of 0.80.0, S-11): a second look at the code that keeps the writer's work safe, by a tool other than
# the one that wrote it and other than the test suites: cppcheck (static analysis) over saving, the save journal,
# the document store, snapshots, backups and the ZIP code, sync safety, program updates, add-ons and signatures.
# Fails on any finding of the kinds error, warning, portability or performance that is not listed, with its reason,
# in tools/code-check-suppressions.txt. The release workflow runs it (job "code-check"); nothing is published
# before it passes.
#   tools/code-check.sh            check
#   QB_CODE_CHECK_ALL=1 ...        also the style findings (to read, they do not fail the check)
set -euo pipefail
cd "$(dirname "$0")/.."
command -v cppcheck >/dev/null || { echo "code-check: cppcheck is not installed (apt-get install cppcheck)" >&2; exit 2; }
files=(
  src/core/SafeSave.h src/core/SaveBatch.cpp src/core/Project.cpp src/core/DocumentStore.cpp
  src/core/Snapshots.cpp src/core/Backup.cpp src/core/Zip.cpp src/core/SyncSafety.cpp src/core/PathSafety.cpp
  src/core/Addons.cpp src/core/Launcher.cpp src/core/Signing.cpp src/core/Package.cpp
)
present=()
for f in "${files[@]}"; do [ -f "$f" ] && present+=("$f"); done
enable="warning,portability,performance"
[ -n "${QB_CODE_CHECK_ALL:-}" ] && enable="$enable,style"
out=$(mktemp)
cppcheck --quiet --std=c++17 --language=c++ --enable="$enable" --inline-suppr \
  --suppressions-list=tools/code-check-suppressions.txt \
  -I src -D QB_TEST_SWITCHES -D Q_OS_LINUX -D Q_OS_UNIX -U Q_OS_WIN -U Q_OS_MACOS \
  --library=qt --suppress=missingIncludeSystem --suppress=unknownMacro \
  --template='{file}:{line}: {severity} ({id}): {message}' "${present[@]}" 2> "$out" || true
failing=$(grep -E ': (error|warning|portability|performance) \(' "$out" || true)
cat "$out"
rm -f "$out"
if [ -n "$failing" ]; then
  echo "code-check: $(printf '%s\n' "$failing" | wc -l) finding(s) above" >&2
  exit 1
fi
echo "code-check: ${#present[@]} files, no findings"
