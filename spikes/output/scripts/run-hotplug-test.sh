#!/usr/bin/env bash
# Hot-plug test without external monitors: two virtual displays stand in for the audience and
# stage displays (build/virtual-display), and this script plugs and unplugs them when
# tests/hotplug.txt logs each "mark". Outputs go fullscreen on the virtual displays.
#
#   ./scripts/run-hotplug-test.sh [borderless|native] [extra app options...]
#
# Settings live in the log dir (--settings-dir), so the saved display choices aren't touched.
set -uo pipefail

cd "$(dirname "$0")/.."
fullscreen=${1:-borderless}
shift || true
app=./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike
vd=./build/virtual-display
log_dir="spike-logs/hotplug-$fullscreen-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$log_dir"

[[ -x "$app" && -x "$vd" ]] || { echo "build the spike first (see README)"; exit 2; }
[[ -f media/1080p30-h264.mp4 ]] || ./scripts/make-test-media.sh

plug() { # name serial [size]
    "$vd" --name "$1" --serial "$2" --size "${3:-1920x1080}" >> "$log_dir/virtual-display.out" 2>&1 &
    eval "pid_$2=$!"
    sleep 1
}
unplug() { # serial
    local var="pid_$1"
    kill "${!var}" 2>/dev/null && wait "${!var}" 2>/dev/null
}
cleanup() {
    for s in 1 2 3; do unplug $s; done
}
trap cleanup EXIT

wait_mark() { # waits until the app logs "mark <name>"; fails if the app exits first
    while ! grep -q "  mark $1\$" "$log_dir/events.log" 2>/dev/null; do
        kill -0 "$app_pid" 2>/dev/null || { echo "app exited before mark $1"; return 1; }
        sleep 0.1
    done
}

plug "Fake Audience" 1
plug "Fake Stage" 2

caffeinate -dimsu -w $$ &
"$app" --fullscreen "$fullscreen" --video media/1080p30-h264.mp4 --video-mode shared --stage-video \
    --script tests/hotplug.txt --log-dir "$log_dir" --settings-dir "$log_dir/settings" "$@" > "$log_dir/app.out" 2>&1 &
app_pid=$!

wait_mark unplug-stage         && unplug 2
wait_mark plug-other           && plug "Other Monitor" 3
wait_mark replug-stage         && unplug 3 && plug "Fake Stage" 2
wait_mark unplug-audience      && unplug 1
wait_mark replug-audience-720p && plug "Fake Audience" 1 1280x720
wait_mark unplug-both          && unplug 1 && unplug 2
wait_mark replug-both          && plug "Fake Audience" 1 && plug "Fake Stage" 2

wait "$app_pid"
status=$?
grep -E "  (ok |FAIL|mark|script done)|place |display " "$log_dir/events.log"
grep -iE "crit|fatal|rror" "$log_dir/app.out" | head -10
echo "exit $status, snapshots: $log_dir/snapshots/"
exit $status
