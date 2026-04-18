#!/usr/bin/env bash
# bench.sh — measure fork solve time and solution shape on a fixture set.
#
# Per handoff/vroom-fork-bench.md: for every fixture, run the binary N times
# and report median/p99 solve time plus the cost/routes/unassigned/distance/
# duration of the last run. CSV on stdout.
#
# Usage:
#   scripts/bench.sh [fixtures-dir]
#
# Env overrides:
#   VROOM_BIN    path to vroom binary (default: ./bin/vroom)
#   BENCH_RUNS   number of runs per fixture (default: 20)
#   VROOM_ARGS   extra arguments passed to vroom (e.g. "-r valhalla -a auto:valhalla:8002")
#
# Fixtures that require a live router (no embedded matrix) will fail unless
# VROOM_ARGS points at a running Valhalla/OSRM. Self-contained fixtures
# (embedded "matrices") run without a router.

set -euo pipefail

FIXTURES="${1:-handoff/vroom-fork-fixtures}"
BINARY="${VROOM_BIN:-./bin/vroom}"
RUNS="${BENCH_RUNS:-20}"
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
problems=("$FIXTURES"/problem-*.json)
if [[ ${#problems[@]} -eq 0 ]]; then
  echo "No problem-*.json files in $FIXTURES" >&2
  exit 1
fi

echo "fixture,shipments,jobs,vehicles,median_ms,p99_ms,cost,routes,unassigned,distance_m,duration_s"

for pfile in "${problems[@]}"; do
  label=$(basename "$pfile" .json)
  shipments=$(jq '(.shipments // []) | length' < "$pfile")
  jobs=$(jq '(.jobs // []) | length' < "$pfile")
  vehicles=$(jq '(.vehicles // []) | length' < "$pfile")

  times_ms=()
  last_solution=""
  for _ in $(seq 1 "$RUNS"); do
    # shellcheck disable=SC2086
    solution=$("$BINARY" $EXTRA_ARGS -i "$pfile" 2>/dev/null)
    t=$(echo "$solution" | jq '.summary.computing_times.solving')
    times_ms+=("$t")
    last_solution="$solution"
  done

  sorted=$(printf '%s\n' "${times_ms[@]}" | sort -n)
  median=$(echo "$sorted" | awk "NR == int(($RUNS + 1) / 2)")
  p99=$(echo "$sorted" | tail -1)

  cost=$(echo "$last_solution" | jq '.summary.cost')
  routes=$(echo "$last_solution" | jq '.routes | length')
  unassigned=$(echo "$last_solution" | jq '.unassigned | length')
  distance=$(echo "$last_solution" | jq '.summary.distance')
  duration=$(echo "$last_solution" | jq '.summary.duration')

  echo "$label,$shipments,$jobs,$vehicles,$median,$p99,$cost,$routes,$unassigned,$distance,$duration"
done
