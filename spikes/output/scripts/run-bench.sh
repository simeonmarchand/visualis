#!/usr/bin/env bash
# Benchmark run with the outputs fullscreen on two virtual displays (no external monitors needed).
# Slides advance every 2 s; prints the summary at the end. Keeps the Mac awake while it runs.
#
#   ./scripts/run-bench.sh <video file> <seconds> [extra app options, e.g. --video-mode per-window]
set -uo pipefail

cd "$(dirname "$0")/.."
video=${1:?video file}
seconds=${2:?seconds}
shift 2
app=./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike
vd=./build/virtual-display
log_dir="spike-logs/bench-$(basename "${video%.*}")-${seconds}s-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$log_dir"

[[ -x "$app" && -x "$vd" ]] || { echo "build the spike first (see README)"; exit 2; }
[[ -f "$video" ]] || { echo "no such video: $video (run scripts/make-test-media.sh)"; exit 2; }

"$vd" --name "Fake Audience" --serial 1 > /dev/null 2>&1 & vd1=$!
"$vd" --name "Fake Stage" --serial 2 > /dev/null 2>&1 & vd2=$!
trap 'kill $vd1 $vd2 2>/dev/null' EXIT
sleep 2

cat > "$log_dir/script.txt" <<EOF
assign audience Fake Audience||
assign stage Fake Stage||
wait-for video-status ~ playing 10000
mark measuring
wait $((seconds * 1000))
EOF

caffeinate -dimsu -w $$ &
"$app" --video "$video" --stage-video --auto-advance 2 --script "$log_dir/script.txt" \
    --log-dir "$log_dir" --settings-dir "$log_dir/settings" "$@" > "$log_dir/app.out" 2>&1
status=$?
./scripts/summarize.py "$log_dir"
exit $status
