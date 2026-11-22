#!/usr/bin/env bash
# Crash recovery test (#25; Phase 1 acceptance): kill the app mid-song, relaunch, and it must
# be back on the same slide within 10 s with the outputs on the right displays. Then (#51)
# mid-service: back on the same item and slide, and Next carries on into the next item. Two virtual
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

quit_app() {
    osascript -e "tell application id \"org.visualis.Visualis\" to quit" > "$work/quit.out" 2>&1
    for _ in $(seq 50); do kill -0 "$app_pid" 2>/dev/null || break; sleep 0.1; done
    kill -0 "$app_pid" 2>/dev/null && kill -9 "$app_pid"
    wait "$app_pid" 2>/dev/null
    app_pid=
}
journal_at() { # item number, slide index
    grep -q "\"item\":\"$(item "$1")\"" "$journal" 2>/dev/null && grep -q "\"slideIndex\":$2," "$journal"
}

# A service of 4 songs (3 slides each) with a header after the second, as Visualis exports.
exports="$work/exports"
mkdir -p "$exports"
for n in 1 2 3 4; do
    cat > "$exports/Song $n.visualis-song.json" <<EOF
{"format":"visualis-song","version":1,"id":"00000000-0000-4000-8000-00000000000$n","title":"Service Song $n",
 "groups":[{"label":"Verse","slides":["$n one","$n two"]},{"label":"Chorus","slides":["$n three"]}]}
EOF
done
item() { echo "00000000-0000-4000-9000-00000000000$1"; }
song_item() { # item number, song number
    echo "{\"id\":\"$(item "$1")\",\"kind\":\"song\",\"title\":\"Service Song $2\",\"ref\":\"00000000-0000-4000-8000-00000000000$2\"}"
}
cat > "$exports/Sunday.visualis-service.json" <<EOF
{"format":"visualis-service","version":1,"id":"00000000-0000-4000-a000-000000000001","title":"Crash Test Sunday",
 "items":[$(song_item 0 1),$(song_item 1 2),{"id":"$(item 2)","kind":"header","title":"Offering"},$(song_item 3 3),$(song_item 4 4)]}
EOF

echo "== mid-service: the second song's last slide, kill -9, relaunch, Next"
launch "$work/run3.log" --import "$exports" --command goto_item=1 --command next_slide --command next_slide
for _ in $(seq 50); do journal_at 1 2 && break; sleep 0.1; done
journal_at 1 2 && pass "journal has the service item and slide" || fail "journal not at the second song's last slide"
kill -9 "$app_pid"; wait "$app_pid" 2>/dev/null
launch "$work/run4.log" --command next_slide
wait_for "$work/run4.log" '"Service Song 2", slide 3 of 3' 10 >/dev/null \
    && pass "back on the second song's last slide" || fail "not back on the same item and slide"
# Next crosses the header into the third song: the service came back, not just the song.
for _ in $(seq 50); do journal_at 3 0 && break; sleep 0.1; done
journal_at 3 0 && pass "Next goes on to the next song after a recovery" || fail "Next didn't reach the next song"
quit_app
[[ ! -e "$journal" ]] && pass "journal removed on a clean quit" || fail "journal still there after quitting"

grep -iE "critical|fatal" "$work"/run*.log | head -5
echo "$passed passed, $failed failed. Logs: $work"
((failed == 0))
