#!/usr/bin/env bash
# test-counterfactual.sh — Busportal fork, M6 / F6. Run every
# `problem-*.json` under $FIXTURES through `bin/vroom --counterfactual`
# and assert:
#   - The output parses as JSON with the RFC §5.6.2 top-level keys
#     (baseline_solution, modified_solution, diff, improvement).
#   - `improvement.applied_what_if` matches the what_if key the fixture
#     carries (so first-one-wins ordering is respected).
#   - `improvement.new_total_cost` equals `modified_solution.summary.cost`
#     (end-to-end consistency: the improvement summary references the
#     same numbers the solution reports).
#   - `diff.summary_diff.total_cost_change` equals
#     `improvement.cost_change` (the diff and the improvement agree on
#     the delta).
#   - `improvement.solve_time_ms_baseline` and
#     `improvement.solve_time_ms_modified` are both < 500 ms
#     (RFC §5.6.3 says total wall-clock ≤ 2× single-solve; at sandbox
#     synthetic sizes each solve is well under 100 ms, 500 ms is a
#     conservative upper bound).
#
# Usage:
#   scripts/test-counterfactual.sh [fixtures-dir]

set -euo pipefail

FIXTURES="${1:-tests/fixtures/counterfactual}"
BINARY="${VROOM_BIN:-./bin/vroom}"
MAX_SOLVE_MS="${MAX_SOLVE_MS:-500}"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing binary: $BINARY" >&2
  exit 1
fi
if [[ ! -d "$FIXTURES" ]]; then
  echo "No fixtures directory: $FIXTURES" >&2
  exit 1
fi

shopt -s nullglob
found=0
fail=0
for pfile in "$FIXTURES"/problem-*.json; do
  label=$(basename "$pfile" .json | sed 's/^problem-//')
  found=$((found + 1))

  if ! actual=$("$BINARY" --counterfactual -i "$pfile" 2>/dev/null); then
    echo "FAIL $label: binary error"
    fail=$((fail + 1))
    continue
  fi

  # Detect the first what_if key per RFC §5.6.2 enumeration — that's
  # what the implementation should report as `applied_what_if`.
  expected_key=$(jq -r '
    .what_if as $w
    | ["add_vehicles","remove_vehicles","relax_time_windows","add_shipments","remove_shipments"][]
    | select(. as $k | $w[$k] != null)
  ' "$pfile" | head -1)
  expected_key="${expected_key:-none}"

  diff_msg=$(echo "$actual" | jq --arg exp "$expected_key" --argjson max "$MAX_SOLVE_MS" '
    . as $out
    | [
        if ($out | has("baseline_solution") and has("modified_solution") and has("diff") and has("improvement")) | not then
          "missing top-level keys"
        else empty end,
        if $out.improvement.applied_what_if != $exp then
          "applied_what_if=\($out.improvement.applied_what_if), expected=\($exp)"
        else empty end,
        if $out.improvement.new_total_cost != $out.modified_solution.summary.cost then
          "new_total_cost vs modified.summary.cost mismatch"
        else empty end,
        if $out.improvement.cost_change != $out.diff.summary_diff.total_cost_change then
          "cost_change vs summary_diff mismatch (\($out.improvement.cost_change) vs \($out.diff.summary_diff.total_cost_change))"
        else empty end,
        if $out.improvement.solve_time_ms_baseline > $max or $out.improvement.solve_time_ms_modified > $max then
          "solve_time > \($max) ms"
        else empty end
      ]
    | join("; ")
  ')
  if [[ -n "$diff_msg" && "$diff_msg" != "null" && "$diff_msg" != "\"\"" ]]; then
    clean=${diff_msg//\"/}
    if [[ -n "$clean" ]]; then
      echo "FAIL $label: $clean" >&2
      fail=$((fail + 1))
      continue
    fi
  fi

  applied=$(echo "$actual" | jq -r '.improvement.applied_what_if')
  additional=$(echo "$actual" | jq -r '.improvement.additional_assigned')
  cost_change=$(echo "$actual" | jq -r '.improvement.cost_change')
  base_ms=$(echo "$actual" | jq -r '.improvement.solve_time_ms_baseline')
  mod_ms=$(echo "$actual" | jq -r '.improvement.solve_time_ms_modified')
  echo "OK   $label (applied=$applied additional=$additional cost_change=$cost_change base_ms=$base_ms mod_ms=$mod_ms)"
done

if [[ $found -eq 0 ]]; then
  echo "No counterfactual fixtures found in $FIXTURES" >&2
  exit 2
fi
if [[ $fail -gt 0 ]]; then
  echo "$fail counterfactual regression(s) detected" >&2
  exit 1
fi
echo "all $found counterfactual fixtures OK"
