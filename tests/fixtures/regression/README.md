# Self-contained regression fixtures

Problem/solution pairs used by `scripts/regression.sh` in CI. Every fixture
in this directory MUST embed its own `matrices` block so the regression
check runs without a live router (no Valhalla, no OSRM).

## Current fixtures

| Fixture | Source | Notes |
|---|---|---|
| `problem-example-2.json` / `solution-example-2.json` | Upstream `docs/example_2.json` (+ `_sol`) | Smallest self-contained VRPTW; exercises matrix input, time windows, skills. |
| `problem-cost-breakdown.json` / `solution-cost-breakdown.json` | Busportal fork, M1 | Exercises the F3 `cost_breakdown` invariant: non-zero `fixed_vehicle`, `duration`, `distance`, and `task` buckets (per_hour=7200, per_km=500, per_task_hour=1800, fixed=1000). |
| `problem-custom-cost-matrix.json` / `solution-custom-cost-matrix.json` | Busportal fork, M1 | Exercises the non-`cost_based_on_metrics` path: a user-supplied `costs` matrix makes duration/distance attribution meaningless, so the full travel cost lands in the `duration` bucket. |
| `problem-unassigned-capacity.json` + diagnostics | Busportal fork, M2 | Job whose delivery exceeds every vehicle's capacity; expects `capacity_exceeded`. |
| `problem-unassigned-skills.json` + diagnostics | Busportal fork, M2 | Job whose required skills no vehicle has; expects `no_vehicle_with_required_skills`. |
| `problem-unassigned-tw.json` + diagnostics | Busportal fork, M2 | Job whose time window no vehicle can reach in time; expects `time_window_infeasible`. |
| `problem-embedded-shipments-3.json` / `-4` / `-5` | Busportal handoff, M3 prep | Real Lviv dispatch shipments with Valhalla-derived matrices embedded so the harness works offline. Source: `handoff/vroom-fork-fixtures/` on `handoff/initial-briefing`. |
| `problem-co-located-group.json` | Busportal fork, M3 | 3 pickups at stop A + 1 at stop B; exercises F1 service-time dedup when all land on one vehicle consecutively. |
| `problem-co-located-split.json` | Busportal fork, M3 | 4 pickups at one stop with a capacity limit that forces `pickup-pickup-delivery-delivery-pickup-pickup-delivery-delivery`; exercises per-run dedup on a single vehicle with two co-located runs. |
| `problem-co-located-tw-stagger.json` | Busportal fork, M3.1 | Three same-coord pickups with staggered TW-starts; common arrival respects every member's earliest-start. |
| `problem-co-located-breakdown-reaccum.json` | Busportal fork, M3.1 | Multi-bucket breakdown + dedup; catches the "partial summary re-accumulation" bug. |
| `problem-soft-tw-shift-late.json` | Busportal fork, M4 | Pickup pushed from earliest feasible (500) to preferred (6000); zero soft cost. |
| `problem-soft-tw-before-preferred.json` | Busportal fork, M4 | Downstream hard TW caps pickup below `preferred_start`; pays `cost_per_second_before`. |
| `problem-soft-tw-after-preferred.json` | Busportal fork, M4 | Earliest feasible past `preferred_end`; pays `cost_per_second_after` (shift-late pass does not pull earlier). |
| `problem-synthetic-30.json` | Busportal fork, M4 (bench coverage) | Deterministic 28-shipment synthetic (`scripts/gen-synthetic-30.py`, seed 42). 20 regular + 5 co-located + 3 confirmed; some co-located carry `soft_time_window`. Bench fixture for the RFC §8 performance budget. |

## Diagnostics expectations

Files under `diagnostics/<label>.json` let `scripts/regression.sh`
re-run the matching problem with `-d` / `--diagnostics` and assert the
per-id reason code. Schema:

```json
{
  "unassigned": [
    {"id": 42, "reason": "<reason_code>"}
  ]
}
```

## Adding a fixture

1. Ensure the problem JSON contains a `matrices` block keyed by profile —
   never coordinate-based locations alone.
2. Name the files `problem-<label>.json` and `solution-<label>.json`.
3. Record the solution with the **currently-released** fork binary, not a
   feature branch, so the baseline reflects the last released behavior.
4. Verify `scripts/regression.sh tests/fixtures/regression` passes locally.

## Busportal handoff fixtures

The larger handoff set at `handoff/vroom-fork-fixtures/` (not in this repo
by default — copied from the `handoff/initial-briefing` branch) uses live
Valhalla profiles and cannot run here. Use those fixtures only against a
deployed stack with Valhalla reachable, via
`VROOM_ARGS='-r valhalla -a auto:valhalla:8002' scripts/regression.sh ...`.
