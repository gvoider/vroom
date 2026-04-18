#!/usr/bin/env bash
# regression.sh — compare fork output against known-good solution JSONs.
#
# Per handoff/vroom-fork-bench.md: for every problem-*.json in the fixtures
# directory, solve it with the fork binary and compare the resulting
# summary.cost (and unassigned count) to the recorded solution-*.json.
# Exits non-zero on any mismatch.
#
# Usage:
#   scripts/regression.sh [fixtures-dir]
#
# Env overrides:
#   VROOM_BIN    path to vroom binary (default: ./bin/vroom)
#   VROOM_ARGS   extra arguments (e.g. "-r valhalla -a auto:valhalla:8002")
#
# Fixtures without embedded matrices will fail unless VROOM_ARGS wires up
# a router. CI runs this against the self-contained docs/example_*.json
# fixtures; the handoff/vroom-fork-fixtures set requires a live Valhalla.

set -euo pipefail

FIXTURES="${1:-handoff/vroom-fork-fixtures}"
BINARY="${VROOM_BIN:-./bin/vroom}"
EXTRA_ARGS="${VROOM_ARGS:-}"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing binary: $BINARY" >&2
  echo "Build first: (cd src && make -j)" >&2
  exit 1
fi

if [[ ! -d "$FIXTURES" ]]; then
  echo "No fixtures directory: $FIXTURES" >&2
  exit 1
fi

shopt -s nullglob
fail=0
found=0
for pfile in "$FIXTURES"/problem-*.json; do
  label=$(basename "$pfile" .json | sed 's/^problem-//')
  expected="$FIXTURES/solution-$label.json"
  if [[ ! -f "$expected" ]]; then
    echo "skip $label: no recorded solution"
    continue
  fi
  found=$((found + 1))

  # shellcheck disable=SC2086
  if ! actual=$("$BINARY" $EXTRA_ARGS -i "$pfile" 2>/dev/null); then
    echo "FAIL $label: solver error"
    fail=$((fail + 1))
    continue
  fi

  exp_cost=$(jq '.summary.cost' "$expected")
  act_cost=$(echo "$actual" | jq '.summary.cost')
  exp_unassigned=$(jq '.unassigned | length' "$expected")
  act_unassigned=$(echo "$actual" | jq '.unassigned | length')
  exp_routes=$(jq '.routes | length' "$expected")
  act_routes=$(echo "$actual" | jq '.routes | length')

  if [[ "$exp_cost" != "$act_cost" || "$exp_unassigned" != "$act_unassigned" || "$exp_routes" != "$act_routes" ]]; then
    echo "FAIL $label: expected cost=$exp_cost routes=$exp_routes unassigned=$exp_unassigned" >&2
    echo "         got cost=$act_cost routes=$act_routes unassigned=$act_unassigned" >&2
    fail=$((fail + 1))
    continue
  fi

  # Cost-breakdown invariant (F3, added in M1): every component sums to
  # the route cost within one integer unit; the summary is the sum of
  # route breakdowns.
  if echo "$actual" | jq -e '.summary.cost_breakdown' >/dev/null 2>&1; then
    bd_drift=$(echo "$actual" | jq '
      def sum_bd(bd): bd.fixed_vehicle + bd.duration + bd.distance
                      + bd.task + bd.priority_bias
                      + bd.soft_time_window_violation
                      + bd.published_vehicle_deviation;
      [
        (.summary.cost - sum_bd(.summary.cost_breakdown) | fabs),
        (.routes[] | .cost - sum_bd(.cost_breakdown) | fabs)
      ] | max')
    if (( $(echo "$bd_drift > 1" | bc -l) )); then
      echo "FAIL $label: cost_breakdown drift=$bd_drift > 1 unit" >&2
      fail=$((fail + 1))
      continue
    fi
    echo "OK   $label (cost=$act_cost routes=$act_routes unassigned=$act_unassigned breakdown_drift=$bd_drift)"
  else
    echo "OK   $label (cost=$act_cost routes=$act_routes unassigned=$act_unassigned)"
  fi
done

if [[ $found -eq 0 ]]; then
  echo "No problem/solution pairs found in $FIXTURES" >&2
  exit 2
fi

if [[ $fail -gt 0 ]]; then
  echo "$fail regression(s) detected" >&2
  exit 1
fi

echo "all $found fixtures OK"
