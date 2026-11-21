#!/usr/bin/env bash
# Sends real key presses and one mouse click to the spike through macOS System Events,
# while the app checks each result (tests/keyboard-expect.txt).
# Needs Accessibility permission for the app this runs in (System Settings > Privacy &
# Security > Accessibility). Don't touch the keyboard or mouse for about 30 s.
set -uo pipefail

cd "$(dirname "$0")/.."
app=./build/visualis-output-spike.app/Contents/MacOS/visualis-output-spike
process=visualis-output-spike
log_dir="spike-logs/keyboard-$(date +%Y%m%d-%H%M%S)"
mkdir -p spike-logs

[[ -x "$app" ]] || { echo "build the spike first (see README)"; exit 2; }
if [[ "$(osascript -e 'tell application "System Events" to return UI elements enabled' 2>/dev/null)" != "true" ]]; then
    echo "Accessibility permission is off for this terminal app."
    echo "Turn it on in System Settings > Privacy & Security > Accessibility, then rerun."
    exit 2
fi

"$app" --script tests/keyboard-expect.txt --log-dir "$log_dir" --settings-dir "$log_dir/settings" > "$log_dir.out" 2>&1 &
app_pid=$!

# Wait for the operator window to exist.
for _ in $(seq 50); do
    osascript -e "tell application \"System Events\" to exists (window 1 of process \"$process\")" 2>/dev/null | grep -q true && break
    sleep 0.2
done
sleep 1

send() { # AppleScript statement for System Events, then a pause for the app to react
    osascript -e "tell application \"System Events\" to tell process \"$process\"
        set frontmost to true
        $1
    end tell" || echo "osascript failed: $1"
    sleep 0.8
}

# macOS key codes: 123 Left, 124 Right, 125 Down, 126 Up, 116 Page Up, 121 Page Down, 53 Esc, 49 Space
send 'key code 124'      # Right     -> 2
send 'key code 49'       # Space     -> 3
send 'key code 121'      # Page Down -> 4
send 'key code 123'      # Left      -> 3
send 'key code 126'      # Up        -> 2
send 'key code 116'      # Page Up   -> 1
send 'key code 125'      # Down      -> 2
send 'keystroke "c"'     # clear text
send 'key code 124'      # Right     -> 3, text back
send 'keystroke "b"'     # background off
send 'keystroke "b"'     # background on
send 'key code 53'       # Esc: clear all

# Row 8: a real mouse click on Previous (the app logs where it is), then Space.
# An accessibility "press" wouldn't do: the point is whether a mouse click leaves focus on the button.
for _ in $(seq 50); do
    point=$(grep -o "located prevButton at .*" "$log_dir/events.log" 2>/dev/null | awk '{print $4, $5}')
    [[ -n "$point" ]] && break
    sleep 0.1
done
if [[ -n "$point" ]]; then
    send ''   # bring the operator window to the front
    osascript -l JavaScript - $point <<'JXA' || echo "mouse click failed"
ObjC.import('CoreGraphics')
function run(argv) {
    const p = $.CGPointMake(Number(argv[0]), Number(argv[1]))
    for (const type of [$.kCGEventLeftMouseDown, $.kCGEventLeftMouseUp]) {
        $.CGEventPost($.kCGHIDEventTap, $.CGEventCreateMouseEvent(null, type, p, $.kCGMouseButtonLeft))
        delay(0.05)
    }
}
JXA
    sleep 0.8
else
    echo "the app never reported where the Previous button is"
fi
send 'key code 49'

wait "$app_pid"
status=$?

grep -E "  (ok |FAIL|script done)" "$log_dir/events.log"
grep -E "qrc:|QML|rror" "$log_dir.out" | head -20
echo "snapshots: $log_dir/snapshots/"
exit $status
