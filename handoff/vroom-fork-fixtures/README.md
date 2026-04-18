# VROOM Fork — Test Fixtures

Real VROOM problem/solution JSON pairs captured from `backend-dispatch` dev pod logs on 2026-04-18.

## Contents

| File | Description |
|---|---|
| `problem-shipments-3.json` | 3-shipment day, 2 routes in solution, 2 shipments unassigned |
| `solution-shipments-3.json` | Paired mainline VROOM v1.13.0 solution for the above |
| `problem-shipments-4.json` | 4-shipment day |
| `solution-shipments-4.json` | Paired solution |
| `problem-shipments-5.json` | 5-shipment day |
| `solution-shipments-5.json` | Paired solution |

Each problem JSON is the exact payload our consumer (`VroomClient::solve`) sent to VROOM. Each solution JSON is what VROOM v1.13.0 returned.

## Provenance and caveats

- Captured from `backend-dispatch-76bdbfddd9-fklzj` pod logs via `kubectl logs --tail=500 … | grep -A1 'Problem (unassigned'`.
- Logs only include problems where at least one shipment came back unassigned (see `VroomClient::solve`, lines 673-688 — dumps are gated on `unassigned > 0` to avoid log spam).
- As a result, **every fixture has >=1 unassigned shipment**. Successful-all-assigned problems are not captured in these fixtures. For pure-success regression you'll need to synthesize.
- Descriptions on pickup/delivery steps have been replaced with `[sanitized-pickup-NN]` to remove passenger-identifying text.
- Coordinates are preserved (they're geographic, not identifying in isolation).
- Request IDs are preserved.

## Size limitation

These are small problems (3-5 shipments, 2-3 vehicles). **Our real workload ranges up to 50 shipments per solve**; larger problems weren't captured in this log window because none had infeasibility.

Before shipping any milestone that affects performance (F1 co-located batching especially), the engineer SHOULD:
1. Capture a larger problem in a fresh log dump, OR
2. Synthesize a 20–50 shipment problem using the observed coordinate bounding box (`lat 49.0–51.0`, `lng 23.0–27.0`) and typical vehicle fleet (2–3 vehicles, capacity `[4, 3]`).

## How to use

### As regression inputs
```bash
./bin/vroom -i problem-shipments-3.json > my-solution.json
diff <(jq -S . my-solution.json) <(jq -S . solution-shipments-3.json)
```
Should produce no diff on mainline. On the fork, differences are allowed only on newly-added fields (e.g. `cost_breakdown`, structured `unassigned.reason`).

### As milestone-acceptance fixtures
Per-milestone, extend the fixture set with named cases that target the feature:

- **M1 (cost_breakdown)**: use these same fixtures; assert the breakdown sums to `summary.cost`.
- **M2 (diagnostics)**: these all have unassigned; assert each has a structured reason code.
- **M3 (co_located)**: add a new fixture where 2+ shipments share coords. Easiest approach: pick `problem-shipments-5.json`, duplicate one pickup's coords to another.
- **M4 (soft windows)**: take `problem-shipments-5.json`, add `soft_time_window` to one pickup, assert the solver prefers the preferred interval.
- **M5 (/diff)**: solve twice (with and without a vehicle); pass both solutions to `/diff`.

### Structure notes

Shipments in our problems:
- Have integer `id` matching our `DispatchRequest.id`.
- Use `priority: 100` for already-confirmed passengers (hard-tight time windows `[confirmedSec - 300, confirmedSec + 300]`).
- Use `priority: 50` for the tentative-edit passenger.
- Use `priority: 10` for other pending.
- `amount`: `[passengerCount, luggageUnits]` (2D capacity).
- `service` field on pickup is currently `0` (per our recent [backend-dispatch] change to collapse co-located passengers).

Vehicles:
- Have `end` (station coords) but NO `start` (free-floating).
- `speed_factor: 0.85` (pads Valhalla's optimistic travel times).
- `costs.fixed: 100000` (strong bias to use fewer vehicles).
- `capacity: [N, M]` from `maxPassengers` + `trunkVolumeLiters / 100`.
- `time_window: [86400, 172800]` (00:00–23:59 today in synthetic-day-offset seconds).

## Embedded-matrix fixtures (added 2026-04-18)

The three `problem-embedded-shipments-*.json` files are drop-in replacements for the originals
with one addition: every problem carries a full `matrices.auto.{durations, distances}` block
computed from the **real production Valhalla backend** (172.16.144.55:8002) against the exact
lat/lng set in each fixture. Vehicle `start_index`/`end_index` and every step's `location_index`
are wired up.

**Use these when your environment has no routing backend.** All three solve on the fork image
with no Valhalla connection, in ≤ 500 ms, with the same `code: 0 / routes / unassigned` shape
as the originals. Cost-breakdown invariant holds on every one.

These are the intended benchmark inputs for the M3 "≤ 500 ms median on 30-shipment problems"
acceptance gate, in combination with larger synthetic problems (the fork's `scripts/gen-synthetic.py`
describes how to compose bigger cases on top of these coordinates).

Round-trip to Valhalla was done once (this commit); future matrix regeneration should re-run
the script in `ref/handoff/vroom-fork-fixtures-regen.py` in the `busportal1` consumer repo.
