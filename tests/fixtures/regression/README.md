# Self-contained regression fixtures

Problem/solution pairs used by `scripts/regression.sh` in CI. Every fixture
in this directory MUST embed its own `matrices` block so the regression
check runs without a live router (no Valhalla, no OSRM).

## Current fixtures

| Fixture | Source | Notes |
|---|---|---|
| `problem-example-2.json` / `solution-example-2.json` | Upstream `docs/example_2.json` (+ `_sol`) | Smallest self-contained VRPTW; exercises matrix input, time windows, skills. |

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
