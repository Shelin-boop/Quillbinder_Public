#!/bin/bash
# Runs one step of the release workflow and keeps its output (0.50):
#   tools/ci-step.sh <name> <command> [arguments ...]
# The output goes to the log as usual and to /tmp/ci-log/<name>.log; when a step fails, the
# workflow's last step shows the end of that log as an error annotation, which can be read through
# GitHub's checks API (the run's logs themselves need a signed-in browser).
set -o pipefail
name=$1
shift
mkdir -p /tmp/ci-log
echo "$name" > /tmp/ci-log/last-step
"$@" 2>&1 | tee "/tmp/ci-log/$name.log"
rc=${PIPESTATUS[0]}
echo "(exit code $rc)" >> "/tmp/ci-log/$name.log"
exit "$rc"
