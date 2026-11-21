#!/usr/bin/env bash
# Sleep in the middle of playback, with the outputs on two virtual displays.
#
#   ./scripts/run-sleep-test.sh display   displays sleep (pmset displaysleepnow), woken after 15 s. Unattended.
#   ./scripts/run-sleep-test.sh system    the Mac sleeps (pmset sleepnow). Press a key to wake it after
#                                          about 15 s. The app waits up to 10 minutes.
set -uo pipefail

cd "$(dirname "$0")/.."
kind=${1:?display or system}
[[ "$kind" == display || "$kind" == system ]] || { echo "display or system"; exit 2; }
app=./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike
vd=./build/virtual-display
log_dir="spike-logs/sleep-$kind-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$log_dir"
[[ -x "$app" && -x "$vd" ]] || { echo "build the spike first (see README)"; exit 2; }

"$vd" --name "Fake Audience" --serial 1 > /dev/null 2>&1 & vd1=$!
"$vd" --name "Fake Stage" --serial 2 > /dev/null 2>&1 & vd2=$!
trap 'kill $vd1 $vd2 2>/dev/null' EXIT
sleep 2

"$app" --video media/1080p30-h264.mp4 --stage-video --script tests/sleep.txt \
    --log-dir "$log_dir" --settings-dir "$log_dir/settings" "$@" > "$log_dir/app.out" 2>&1 &
app_pid=$!

until grep -q "  mark sleep$" "$log_dir/events.log" 2>/dev/null; do
    kill -0 $app_pid 2>/dev/null || { echo "app exited early"; break; }
    sleep 0.2
done
if [[ "$kind" == display ]]; then
    pmset displaysleepnow
    sleep 15
    caffeinate -u -t 2   # declares user activity, which wakes the displays
else
    echo "Sleeping now. Wake the Mac with a key press after about 15 s."
    pmset sleepnow
fi

wait $app_pid
status=$?
grep -E "  (ok |FAIL|mark|script done)|display |expos|place " "$log_dir/events.log"
echo "exit $status"
exit $status
