#!/usr/bin/env bash
# Runs the plugin against the fake adapter in each of the situations a real head unit
# meets, and says PASS or FAIL for each. The fake is (re)started with the quirks each
# scenario needs, so this owns dev/fake-adapter.sh while it runs.
#
#   dev/scenarios.sh              every scenario
#   dev/scenarios.sh sticky ...   just the named ones
#
# Scenarios:
#   basic     the example's integration test: picture, Stop then Start, a resize
#   phase13   the first negotiation fails with Phase 13 and the plugin retries
#   sticky    the host dies mid session (kill -9) while the adapter clings to the phone,
#             and the restarted host still gets a picture
#   unplug    the phone leaves mid session and comes back
#
# Needs what dev/fake-adapter.sh needs (sudo, kernel headers). Leaves the fake torn down
# and the example not running. Logs: /tmp/cp-fake-adapter.log, /tmp/cp-example.log.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAKE_LOG=/tmp/cp-fake-adapter.log
APP_LOG=/tmp/cp-example.log
RESULTS=()

fake() { "$REPO/dev/fake-adapter.sh" setup -- "$@" >/dev/null; }

kill_example() {
  local signal="${1:-TERM}" pid
  for pid in $(pgrep -f "bundle/carplay_ex""ample" || true); do
    kill "-$signal" "$pid" 2>/dev/null || true
  done
  sleep 1
}

# Waits up to $3 seconds for $2 (an extended regex) to appear in $1 after line $4.
wait_for() {
  local file="$1" pattern="$2" seconds="$3" from="${4:-0}"
  local deadline=$((SECONDS + seconds))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if [ -f "$file" ] && tail -n +"$((from + 1))" "$file" | grep -qE "$pattern"; then
      return 0
    fi
    sleep 0.5
  done
  echo "  timed out after ${seconds}s waiting for /$pattern/ in $file" >&2
  return 1
}

lines() { [ -f "$1" ] && wc -l <"$1" || echo 0; }

record() { RESULTS+=("$1 $2"); echo "$2: $1"; }

app() {
  kill_example
  : >"$APP_LOG"
  CP_AUTOSTART=1 "$REPO/dev/run-example.sh" --bundle >/dev/null
}

scenario_basic() {
  fake
  (cd "$REPO/example" &&
    flutter test integration_test/adapter_test.dart -d linux 2>&1 | tail -n 3 |
    grep -q "All tests passed")
}

scenario_phase13() {
  fake --phase13-once
  local mark; mark=$(lines "$FAKE_LOG")
  app
  wait_for "$FAKE_LOG" "Phase 13" 30 "$mark" &&
    wait_for "$APP_LOG" "picture is on screen" 30 &&
    wait_for "$FAKE_LOG" "^.* streaming" 5 "$mark"
}

scenario_sticky() {
  fake --sticky-session
  app
  wait_for "$APP_LOG" "picture is on screen" 30 || return 1
  # No goodbye: the adapter keeps the phone, and would ignore the next Open.
  kill_example KILL
  local mark; mark=$(lines "$FAKE_LOG")
  app
  # The new host clears the stale session first, then gets a picture.
  wait_for "$FAKE_LOG" "iPhone disconnects \(host DisconnectPhone\)" 20 "$mark" &&
    wait_for "$APP_LOG" "picture is on screen" 30 &&
    ! tail -n +"$((mark + 1))" "$FAKE_LOG" | grep -q "this Open is ignored"
}

scenario_unplug() {
  fake --unplug-after 4 --replug-after 3
  app
  wait_for "$APP_LOG" "picture is on screen" 30 || return 1
  local mark; mark=$(lines "$APP_LOG")
  wait_for "$APP_LOG" "picture has ended" 15 "$mark" &&
    wait_for "$APP_LOG" "picture is on screen" 20 "$mark"
}

ALL=(basic phase13 sticky unplug)
WANTED=("$@")
[ ${#WANTED[@]} -gt 0 ] || WANTED=("${ALL[@]}")

trap 'kill_example; "$REPO/dev/fake-adapter.sh" teardown >/dev/null 2>&1 || true' EXIT

for name in "${WANTED[@]}"; do
  if ! declare -F "scenario_$name" >/dev/null; then
    echo "unknown scenario: $name (have: ${ALL[*]})" >&2
    exit 2
  fi
  echo "== $name"
  if "scenario_$name"; then record "$name" PASS; else record "$name" FAIL; fi
  kill_example
done

echo
printf '%s\n' "${RESULTS[@]}"
! printf '%s\n' "${RESULTS[@]}" | grep -q FAIL
