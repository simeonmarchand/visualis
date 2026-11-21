#!/usr/bin/env bash
# Crash recovery test (#25; Phase 1 acceptance): kill the app mid-song, relaunch, and it must
# be back on the same slide within 10 s with the outputs on the right displays. Two virtual
# displays stand in for the audience and stage displays (tools/virtual-display). About 30 s.
# Needs a logged-in macOS session, so it runs by hand, not in CI.
#
#   cmake --build --preset debug && ./scripts/crash-test.sh [build dir]
#
# Settings, library, and journal go in a temporary folder; your own aren't touched.
set -uo pipefail

cd "$(dirname "$0")/.."
build=${1:-build/debug}
app="$build/app/Visualis.app/Contents/MacOS/Visualis"
vd="$build/tools/virtual-display"
[[ -x "$app" && -x "$vd" ]] || { echo "build first: cmake --build --preset debug"; exit 2; }

work=$(mktemp -d -t visualis-crash)
journal="$work/live-journal.json"
passed=0 failed=0
pass() { echo "  ok   $1"; passed=$((passed + 1)); }
fail() { echo "  FAIL $1"; failed=$((failed + 1)); }

plug() { "$vd" --name "$1" --serial "$2" >> "$work/virtual-display.out" 2>&1 & pids+=($!); sleep 1; }
cleanup() {
    [[ -n "${app_pid:-}" ]] && kill -9 "$app_pid" 2>/dev/null
    for p in "${pids[@]:-}"; do kill "$p" 2>/dev/null; done
}
trap cleanup EXIT

launch() { # log file, extra options...
    local log=$1; shift
    QT_MESSAGE_PATTERN="%{time process} %{category}: %{message}" "$app" \
        --settings-dir "$work/settings" --library "$work/library" --journal "$journal" \
        --assign "audience=Fake Audience" --assign "stage=Fake Stage" "$@" > "$log" 2>&1 &
    app_pid=$!
}
wait_for() { # log, pattern, seconds: prints the process time of the matching line
    local polls=$(($3 * 10))
    while ((polls-- > 0)); do
        local line; line=$(grep -F -- "$2" "$1" | head -1)
        [[ -n "$line" ]] && { echo "$line" | awk '{print $1}'; return 0; }
        sleep 0.1
    done
    return 1
}

pids=()
plug "Fake Audience" 1
plug "Fake Stage" 2

# A song as if it had been live when an earlier run stopped: 4 slides, on the first.
cat > "$journal" <<EOF
{"format":"visualis-live","version":1,"slideIndex":0,"textCleared":false,"backgroundCleared":false,
 "background":"","savedAt":"$(date -u +%Y-%m-%dT%H:%M:%S.000Z)",
 "song":{"format":"visualis-song","version":1,"title":"Crash Test Song",
   "groups":[{"label":"Verse 1","slides":["one","two"]},{"label":"Chorus","slides":["three"]},
             {"label":"Verse 2","slides":["four"]}],
   "arrangements":[{"name":"Default","sequence":["Verse 1","Chorus","Verse 2"]}]}}
EOF

echo "== first run: restore the seeded journal, then two slides forward"
launch "$work/run1.log" --command next_slide --command next_slide
wait_for "$work/run1.log" 'slide 1 of 4' 10 >/dev/null && pass "restored the seeded slide" || fail "no restore of the seeded slide"
for _ in $(seq 50); do grep -q '"slideIndex":2' "$journal" 2>/dev/null && break; sleep 0.1; done
grep -q '"slideIndex":2' "$journal" && pass "journal follows the commands (slide 3)" || fail "journal not at slide 3"
wait_for "$work/run1.log" 'stage placed on Fake Stage' 10 >/dev/null || fail "outputs never placed in the first run"

echo "== kill -9 mid-song, relaunch"
kill -9 "$app_pid"; wait "$app_pid" 2>/dev/null
start=$(date +%s.%N 2>/dev/null || python3 -c 'import time; print(time.time())')
launch "$work/run2.log"
t_restore=$(wait_for "$work/run2.log" 'slide 3 of 4' 10) && pass "back on slide 3 of 4 at ${t_restore} s" || fail "not back on slide 3"
t_audience=$(wait_for "$work/run2.log" 'audience placed on Fake Audience' 10) && pass "audience on its display at ${t_audience} s" || fail "audience not placed"
t_stage=$(wait_for "$work/run2.log" 'stage placed on Fake Stage' 10) && pass "stage on its display at ${t_stage} s" || fail "stage not placed"
latest=$(printf '%s\n' "${t_restore:-99}" "${t_audience:-99}" "${t_stage:-99}" | sort -n | tail -1)
awk -v t="$latest" 'BEGIN { exit !(t < 10) }' && pass "recovered within 10 s (${latest} s after launch)" || fail "recovery took ${latest} s"

echo "== quit cleanly: nothing to recover next time"
# A Quit request (as from the menu), not a signal: a signal skips the clean-quit path.
# osascript may report "User canceled (-128)" even when it works: Qt cancels macOS's quit
# event and then quits itself. Whether the process ends is what's checked.
osascript -e "tell application id \"org.visualis.Visualis\" to quit" > "$work/quit.out" 2>&1
for _ in $(seq 50); do kill -0 "$app_pid" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$app_pid" 2>/dev/null; then
    fail "app didn't quit when asked ($(cat "$work/quit.out"))"
    kill -9 "$app_pid"
else
    pass "app quit when asked"
    [[ ! -e "$journal" ]] && pass "journal removed on a clean quit" || fail "journal still there after quitting"
fi
wait "$app_pid" 2>/dev/null
app_pid=

grep -iE "critical|fatal" "$work"/run*.log | head -5
echo "$passed passed, $failed failed. Logs: $work"
((failed == 0))
