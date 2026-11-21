#!/usr/bin/env bash
# Runs tests/operator-smoke.txt and prints the checks. Exit code is non-zero if any check failed.
# Windows open on screen for about 40 s; nothing needs to be pressed.
set -uo pipefail

cd "$(dirname "$0")/.."
log_dir="spike-logs/smoke-$(date +%Y%m%d-%H%M%S)"
app=./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike

[[ -x "$app" ]] || { echo "build the spike first (see README)"; exit 2; }
[[ -f media/1080p30-h264.mp4 && -f media/4k30-hevc.mp4 ]] || ./scripts/make-test-media.sh

"$app" --script tests/operator-smoke.txt --log-dir "$log_dir" --settings-dir "$log_dir/settings" > "$log_dir.out" 2>&1
status=$?

grep -E "  (ok |FAIL|script done)" "$log_dir/events.log"
grep -E "qrc:|QML|rror" "$log_dir.out" | head -20
echo "snapshots: $log_dir/snapshots/"
exit $status
