# VROOM Fork — Benchmark Harness

Purpose: a reproducible way to measure solve time + solution quality so every milestone can prove "no regression beyond the budget" (RFC §2 and §8).

## What to measure

Per fixture, per build (mainline upstream VROOM vs. our fork):

| Metric | How |
|---|---|
| Median solve time (ms) | `summary.computing_times.solving` over 20 runs |
| P99 solve time (ms) | max of 20 runs |
| Total cost | `summary.cost` |
| Routes count | `len(summary.routes)` — typo: should be `len(solution.routes)` |
| Unassigned count | `len(solution.unassigned)` |
| Total duration (s) | `summary.duration` |
| Total distance (m) | `summary.distance` |

## Budget (per RFC §2 and §8)

- Median solve time on 30-shipment baseline ≤ **600 ms** after all P0 features merged (current mainline: ~500 ms).
- P99 ≤ **3 s** (current with peel-off: ~2.5 s).
- Memory ≤ 2× mainline baseline.

## Reference script

Commit this as `scripts/bench.sh` on the fork when M0 scaffolding lands:

```bash
#!/bin/bash
# scripts/bench.sh — benchmark fork vs. mainline on fixture set
# Usage: ./scripts/bench.sh [fixtures-dir]

set -euo pipefail

FIXTURES="${1:-../busportal-handoff/handoff/vroom-fork-fixtures}"
BINARY="${VROOM_BIN:-./build/bin/vroom}"
RUNS="${BENCH_RUNS:-20}"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing binary: $BINARY" >&2
  echo "Run 'cmake --build build --target vroom' first." >&2
  exit 1
fi

echo "fixture,shipments,vehicles,median_ms,p99_ms,cost,routes,unassigned,distance_m,duration_s"

for pfile in "$FIXTURES"/problem-*.json; do
  label=$(basename "$pfile" .json)
  shipments=$(jq '.shipments | length' < "$pfile")
  vehicles=$(jq '.vehicles | length' < "$pfile")

  times_ms=()
  last_solution=""
  for i in $(seq 1 "$RUNS"); do
    solution=$("$BINARY" -i "$pfile" -g 2>/dev/null)
    t=$(echo "$solution" | jq '.summary.computing_times.solving')
    times_ms+=("$t")
    last_solution="$solution"
  done

  # compute median + p99
  sorted=$(printf '%s\n' "${times_ms[@]}" | sort -n)
  median=$(echo "$sorted" | awk "NR == int(($RUNS + 1) / 2)")
  p99=$(echo "$sorted" | tail -1)

  cost=$(echo "$last_solution" | jq '.summary.cost')
  routes=$(echo "$last_solution" | jq '.routes | length')
  unassigned=$(echo "$last_solution" | jq '.unassigned | length')
  distance=$(echo "$last_solution" | jq '.summary.distance')
  duration=$(echo "$last_solution" | jq '.summary.duration')

  echo "$label,$shipments,$vehicles,$median,$p99,$cost,$routes,$unassigned,$distance,$duration"
done
```

### Expected baseline output on `v1.13.0` using fixtures in this handoff

Your local numbers will differ by hardware. Run against mainline v1.13.0 FIRST and record the baseline:

```
fixture,shipments,vehicles,median_ms,p99_ms,cost,routes,unassigned,distance_m,duration_s
problem-shipments-3,3,2,<baseline_3_ms>,…
problem-shipments-4,4,2,<baseline_4_ms>,…
problem-shipments-5,5,2,<baseline_5_ms>,…
```

Each milestone PR MUST include a `bench-before.csv` (mainline) and `bench-after.csv` (fork post-milestone) alongside the PR description, and the CI workflow MUST reject the merge if post-milestone `median_ms` exceeds `mainline_median_ms × 1.2` (i.e. > 20% regression).

## Synthetic large-problem generator

The captured fixtures are small (3–5 shipments). For F1/F2/F8 milestones, generate larger:

```python
# scripts/gen-synthetic.py
import json, random

random.seed(42)

def ship(i, lat_center, lng_center, confirmed=False, shared_stop=None):
    lat = lat_center + random.uniform(-0.3, 0.3)
    lng = lng_center + random.uniform(-0.3, 0.3)
    if shared_stop:
        lat, lng = shared_stop
    pickup = {
        "id": i,
        "location": [lng, lat],
        "service": 0,
        "time_windows": [[115200, 158400]] if not confirmed else [[140100, 140700]]
    }
    delivery = {
        "id": i,
        "location": [24.0315, 49.8411],  # Lviv station
        "service": 60,
        "time_windows": [[118800, 158400]]
    }
    return {
        "pickup": pickup,
        "delivery": delivery,
        "amount": [random.randint(1, 3), random.randint(0, 2)],
        "priority": 100 if confirmed else 10
    }

problem = {
    "vehicles": [
        {
            "id": v,
            "profile": "auto",
            "speed_factor": 0.85,
            "end": [24.0315, 49.8411],
            "capacity": [4, 3],
            "time_window": [86400, 172800],
            "costs": {"fixed": 100000}
        } for v in range(1, 4)
    ],
    "shipments": []
}

# 20 regular shipments
for i in range(1, 21):
    problem["shipments"].append(ship(i, 49.8, 24.5))

# 5 shipments sharing one stop (to test F1 co-located)
shared = (50.0, 25.3)
for i in range(21, 26):
    problem["shipments"].append(ship(i, 0, 0, shared_stop=shared))

# 3 confirmed shipments
for i in range(26, 29):
    problem["shipments"].append(ship(i, 49.8, 24.5, confirmed=True))

print(json.dumps(problem, indent=2))
```

Running `python scripts/gen-synthetic.py > synthetic-28.json` produces a 28-shipment problem that stresses F1 (5 co-located), F8 (3 confirmed), and general scale. Use this for the "30-shipment baseline" referenced throughout the RFC.

## CI integration

Benchmark in CI on every PR:

```yaml
# .github/workflows/bench.yml
name: Benchmark
on:
  pull_request:
    branches: [main]
jobs:
  bench:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install build deps
        run: sudo apt-get install -y libboost-all-dev
      - name: Build fork
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
      - name: Checkout fixtures
        run: git clone --depth 1 https://github.com/gvoider/vroom.git -b handoff/initial-briefing briefing
      - name: Run benchmark
        run: ./scripts/bench.sh briefing/handoff/vroom-fork-fixtures > bench-after.csv
      - name: Compare against baseline
        run: python scripts/compare-bench.py bench-after.csv > comparison.md
      - uses: actions/upload-artifact@v4
        with:
          name: bench-results
          path: bench-after.csv
```

## Quality regression detection

Beyond latency, every milestone MUST assert solution quality:

- **Cost**: the fork's solution cost on a feature-unrelated fixture MUST equal mainline's cost (within ±1 unit for integer rounding).
- **Routes/Unassigned**: counts must match.
- **On feature-exercising fixtures**: quality changes are expected; document the delta in the milestone PR.

Sample `scripts/regression.sh`:

```bash
#!/bin/bash
# Compare fork output against known-good solution JSONs for non-feature fixtures
set -euo pipefail

FIXTURES="${1:-../busportal-handoff/handoff/vroom-fork-fixtures}"

for pfile in "$FIXTURES"/problem-*.json; do
  label=$(basename "$pfile" .json | sed 's/^problem-//')
  expected="$FIXTURES/solution-$label.json"
  [[ -f "$expected" ]] || continue

  actual=$(./build/bin/vroom -i "$pfile")

  # Compare summary cost (primary regression signal)
  exp_cost=$(jq '.summary.cost' "$expected")
  act_cost=$(echo "$actual" | jq '.summary.cost')
  if [[ "$exp_cost" != "$act_cost" ]]; then
    echo "REGRESSION in $label: expected cost $exp_cost, got $act_cost" >&2
    exit 1
  fi
  echo "$label: OK (cost $act_cost)"
done
```
