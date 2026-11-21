#!/usr/bin/env bash
# Display hot-plug test without monitors (#19). Two virtual displays stand in for the
# audience and stage displays (tools/virtual-display); this script plugs and unplugs them
# and checks the app's placement log. About a minute. Needs a logged-in macOS session, so
# it runs by hand, not in CI.
#
#   cmake --build --preset debug && ./scripts/hotplug-test.sh [build dir]
#
# Settings go in a temporary folder, so your saved display choices aren't touched.
set -uo pipefail

cd "$(dirname "$0")/.."
build=${1:-build/debug}
app="$build/app/Visualis.app/Contents/MacOS/Visualis"
app_name=Visualis
vd="$build/tools/virtual-display"
[[ -x "$app" && -x "$vd" ]] || { echo "build first: cmake --build --preset debug"; exit 2; }

work=$(mktemp -d -t visualis-hotplug)
log="$work/app.log"
passed=0 failed=0

plug() { # name serial
    "$vd" --name "$1" --serial "$2" >> "$work/virtual-display.out" 2>&1 &
    eval "pid_$2=$!"
    sleep 1
}
unplug() { # serial
    local var="pid_$1"
    [[ -n "${!var:-}" ]] && kill "${!var}" 2>/dev/null && wait "${!var}" 2>/dev/null
    eval "pid_$1="
}
cleanup() {
    [[ -n "${app_pid:-}" ]] && kill "$app_pid" 2>/dev/null
    for s in 1 2 3; do unplug $s; done
}
trap cleanup EXIT

# Checks only log lines written since the last `mark`.
mark() { echo "== $1"; since=$(wc -l < "$log"); }
recent() { tail -n +$((since + 1)) "$log"; }
pass() { echo "  ok   $1"; passed=$((passed + 1)); }
fail() { echo "  FAIL $1"; failed=$((failed + 1)); }
wait_for() { # pattern seconds
    local polls=$(($2 * 10))
    while ((polls-- > 0)); do
        recent | grep -qF -- "$1" && { pass "$1"; return 0; }
        sleep 0.1
    done
    fail "$1 (not within $2 s)"
}
expect_count() { # pattern count
    local n; n=$(recent | grep -cF -- "$1")
    ((n == $2)) && pass "$1: $n" || fail "$1: $n times, expected $2"
}
settled() { wait_for "displays settled" 6; }
# #26: whether the app holds the display-sleep assertion, as macOS reports it.
awake_held() { pmset -g assertions | grep -q "($app_name).*PreventUserIdleDisplaySleep\|Visualis is presenting"; }
expect_awake() { # yes|no
    local polls=20
    while ((polls-- > 0)); do
        if awake_held; then [[ $1 == yes ]] && { pass "displays kept awake"; return; }
        else [[ $1 == no ]] && { pass "display sleep allowed again"; return; }; fi
        sleep 0.1
    done
    fail "display sleep assertion: expected $1"
}

plug "Fake Audience" 1
plug "Fake Stage" 2

since=0
QT_MESSAGE_PATTERN="%{time process} %{category}: %{message}" "$app" --settings-dir "$work/settings" --assign "audience=Fake Audience" --assign "stage=Fake Stage" > "$log" 2>&1 &
app_pid=$!
caffeinate -dimsu -w "$app_pid" &

mark "start: each output on its own display"
wait_for "audience placed on Fake Audience" 10
wait_for "stage placed on Fake Stage" 10
expect_awake yes

mark "unplug stage: it hides at once, before the displays settle"
unplug 2
wait_for "stage: Waiting: display not connected" 1
settled
expect_count "audience: Waiting" 0

mark "a different display appears: stage doesn't move onto it"
plug "Other Monitor" 3
settled
expect_count "stage placed" 0

mark "stage display comes back: stage returns to it"
unplug 3
plug "Fake Stage" 2
wait_for "stage placed on Fake Stage" 6

# Not covered: identical monitors (macOS refuses two virtual displays with the same serial;
# tst_displayplacement covers the rules), and a display coming back at a different resolution. macOS gives a virtual display
# a new UUID when its mode changes, which a real monitor (identified by its EDID) doesn't do.
mark "audience display unplugged and replugged"
unplug 1
wait_for "audience: Waiting: display not connected" 1
plug "Fake Audience" 1
wait_for "audience placed on Fake Audience" 6

mark "stage display flickers (as after wake): one move, after it settles"
for _ in 1 2 3 4; do
    unplug 2
    sleep 0.3
    plug "Fake Stage" 2
done
settled
expect_count "displays settled" 1
expect_count "stage placed on Fake Stage" 1

mark "both displays gone, then both back"
unplug 1
unplug 2
wait_for "audience: Waiting: display not connected" 1
wait_for "stage: Waiting: display not connected" 1
expect_awake no   # no output showing
plug "Fake Audience" 1
plug "Fake Stage" 2
wait_for "audience placed on Fake Audience" 6
wait_for "stage placed on Fake Stage" 6
expect_awake yes

kill -0 "$app_pid" 2>/dev/null && pass "app still running" || fail "app exited"
grep -iE "critical|fatal" "$log" | head -5
echo "$passed passed, $failed failed. Log: $log"
((failed == 0))
