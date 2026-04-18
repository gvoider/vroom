# VROOM Fork — Requirements Document

**Status**: Draft, ready for engineering handoff
**Date**: 2026-04-18
**Target engineer**: human or agent with C++20, CMake, and VRP-solver background
**Base repository (upstream)**: https://github.com/VROOM-Project/vroom (BSD-2-Clause)
**Our fork**: https://github.com/gvoider/vroom
**Consumer**: `backend-dispatch` (`C:\Portal\busportal1\backend-dispatch`) — on GitLab at `gitlab.itnet.lviv.ua/busportal/backend/backend-dispatch`
**Cross-platform note**: the fork lives on GitHub (matching upstream); the consumer service lives on GitLab. Docker images from the fork should be pushed to our GitLab registry (`registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork`) so K8s in the `bus-vdexpress` namespace can pull them with existing credentials.

---

## 0. How to pick up this document

You are about to fork and extend the VROOM vehicle routing solver to close specific
gaps that our passenger-transfer dispatch service has papered over with ~1800 LOC
of PHP workarounds. You do **not** need to read the full backend-dispatch codebase
to do this work, but you MUST understand:

1. **What VROOM does today** — read https://github.com/VROOM-Project/vroom (README, API.md,
   `docs/API.md`) and run the upstream test suite.
2. **What we've hacked around VROOM to accomplish** — read
   `ref/reports/vroom-workarounds-inventory.md` in this repo (~1800 LOC, 33 hacks).
3. **Why we chose a fork over migration** — read `ref/reports/vroom-alternatives-research.md`
   and `ref/reports/dispatch-stability-research.md`.

Ship work in milestones (defined in §7). After each milestone, our `backend-dispatch`
consumer will be updated to use the new feature; if the new feature works in dev,
the milestone is merged to the fork's `main`. If the fork's `main` ever needs to
pull upstream VROOM, rebase-and-repair is preferred over merge to keep history clean.

The fork is a **permanent maintenance commitment** unless individual features are
upstreamed. Upstreaming is **encouraged but not required** for work to ship. Open
an issue on the upstream repo before each feature to gauge maintainer interest.

---

## 1. Executive summary

Fork VROOM to add five P0 features and three P1 features that let our dispatcher
service model its domain natively, eliminating ~510 LOC of PHP workarounds and
adding capabilities we can't currently express.

**P0 features** (must ship):
- **F1** — Native co-located shared-stop batching
- **F2** — Soft time windows
- **F3** — Per-objective cost breakdown in output
- **F4** — Plan diff endpoint (`POST /diff`)
- **F5** — Richer structured unassigned-reason diagnostics

**P1 features** (ship if P0 lands on schedule):
- **F6** — Counterfactual mode (`POST /counterfactual`)
- **F7** — Driver shifts + breaks
- **F8** — Published-vehicle soft stability penalty

**P2 / future work**: re-optimize warm-start API, time-dependent matrix,
per-vehicle cost matrix, multi-trip vehicles, driver fairness.

Estimated total P0 + P1: **7–9 weeks focused work** including tests, docs, and
dev-cluster verification. P0 alone: 5 weeks.

---

## 2. Goals and non-goals

### Goals

- Reduce `backend-dispatch/src/Dispatch` solver-adjacent code by ≥25% (≥450 LOC).
- Keep median solve time for our workload (10–50 shipments, 1–3 vehicles) at or
  under the current VROOM baseline of **500 ms**. No single P0 feature may
  regress median solve time by more than 20%.
- Every new input field MUST be optional. A VROOM-mainline JSON problem MUST solve
  identically on our fork.
- Every P0 feature ships with:
  - C++ unit tests (catch2, matching VROOM's existing test style)
  - A JSON-level integration test fixture (`tests/fixtures/*.json`)
  - Docs patch to `docs/API.md`

### Non-goals

- Upstream merge acceptance is not a shipping blocker. If upstream rejects an
  RFC, ship anyway on our fork.
- Don't rewrite VROOM's metaheuristic. Additions to the cost function are fine;
  new move operators or a new search algorithm are out of scope.
- Don't add features the consumer doesn't ask for. The 1800-LOC inventory is the
  canonical scope. Nice-to-have academic features (time-dependent matrix, driver
  fairness, reinforcement-learning components) are P2 at best.
- Don't break the HTTP JSON contract. `vroom-express` consumers must continue to
  work unchanged if they don't opt in to any new field.

---

## 3. Baseline: what VROOM already does well

The fork keeps all of this. Do not break it.

- Pickup/delivery shipment pairs with shared vehicle constraint
- Multi-dimensional capacity (passengers + luggage units)
- Time windows (hard), per step
- Skills as hard constraint (we use this for pinning)
- Priorities (0–100) as cost bias
- Per-vehicle `costs.fixed`
- Vehicle `speed_factor` and custom matrix
- `vehicle.time_window`
- Valhalla/OSRM/ORS integration via matrix computation
- Route geometry in solution output
- `unassigned` array with coarse reason

Current solve time on our workload: **100–500 ms median**; with our 5-iteration peel loop,
worst case is 2.5 s. This is our performance floor.

---

## 4. P0 Feature specifications

Each feature MUST include: JSON input schema, JSON output schema, semantics, worked examples,
test matrix, and acceptance criteria.

### F1 — Native co-located shared-stop batching

#### 4.1.1 Purpose

VROOM today treats every shipment independently. Two passengers at the same
coordinates become two shipments, each charged its own service time, often
routed to different vehicles with different arrival times. Our PHP
`DispatchSharedPickupBatcher` preprocesses shipments into synthetic merged
ones before sending to VROOM; this is ~460 LOC and requires an iterative
peel-off re-solve loop (~80 LOC) when merged batches are infeasible.

Fork goal: let VROOM understand "these shipments share a physical stop; if
they end up on the same vehicle, charge service time once and treat as one
arrival event."

#### 4.1.2 Input schema

```json
{
  "shipments": [
    {
      "id": 42,
      "pickup": {
        "id": 42,
        "location": [25.343597, 50.766658],
        "service": 300,
        "time_windows": [[140100, 140700]],
        "co_located_group": "stop:vs-42"
      },
      "delivery": { ... }
    },
    {
      "id": 43,
      "pickup": {
        "id": 43,
        "location": [25.343597, 50.766658],
        "service": 300,
        "time_windows": [[140000, 140600]],
        "co_located_group": "stop:vs-42"
      },
      "delivery": { ... }
    }
  ]
}
```

`co_located_group` is an OPTIONAL string. Two pickups share a co-location
identity if and only if they have the same non-null `co_located_group` string.
Absent or empty means "not co-located."

#### 4.1.3 Semantics

The unit of deduplication is a **maximal consecutive run** of group members on
a vehicle's route — i.e. the group members visited back-to-back with no
non-group step between them. When two or more pickup steps with the same
`co_located_group` form a consecutive run on one vehicle's route:

1. **Their arrival times MUST be equal within the run.** The solver treats
   the run as a single arrival event. When group members on the same
   vehicle are NOT consecutive (a capacity constraint can force a delivery
   between two group pickups, see the split example below), each
   consecutive run deduplicates independently — there is no cross-run
   arrival equalization because that would require moving the interleaved
   step, which is not in scope for the fork.
2. **The common arrival time MUST respect every group member's own
   `time_windows[0].start`.** If the group's members have staggered
   TW-starts, the run's common arrival is pushed forward to the latest
   TW-start among them so no member arrives before its own hard window.
3. **Only ONE of the pickups' `service` times is charged to the route
   duration.** The largest service value among the run's members is used.
   In the solution output that pooled service time is recorded on the
   LAST member of the run (all other members carry `service: 0`), so the
   upstream step-timing invariant
   `arrival[k] - arrival[0] == duration[k] + cumulative(setup+service+waiting) before k`
   continues to hold.
4. **Soft hint to the search**: the solver SHOULD prefer to co-locate
   group members on one vehicle because it saves service time. No
   explicit new cost term is needed — the existing duration-minimizing
   objective already rewards this when service-time deduplication
   applies and the travel cost between same-location steps is zero.
5. If group members end up on DIFFERENT vehicles, each is treated as an
   independent pickup with its own service time. No penalty for
   splitting — we might have no choice given capacity.

Pickups in the same `co_located_group` MUST have the same `location` to within
5 decimal places (~1.1 m). If they don't, the solver MUST reject the problem
with error `co_located_group members at different locations`.

#### 4.1.4 Output schema

Solution output unchanged per-step. Add to `summary.computing_times`:
```json
"computing_times": { ..., "co_location_savings_seconds": 600 }
```
showing how much service-time saving the feature produced on the winning solution.

#### 4.1.5 Examples

**Worked example 1**: Three passengers at stop `vs-42` (coords `[25.34, 50.77]`),
same vehicle. Without the feature: 3 × 300 s = 900 s of service time. With the
feature: 1 × 300 s = 300 s. Saves 600 s of duration.

**Worked example 2**: Three passengers at stop `vs-42`, two on Vehicle 1 and
one on Vehicle 2. Vehicle 1's route pays 300 s (one charge for two co-located).
Vehicle 2's route pays 300 s. Total 600 s — vs 900 s without co-location.

**Worked example 3**: Same coords but different `co_located_group` strings. Treated
as two separate stops; no deduplication. This lets us model "two different services
at the same building" if we ever need it.

#### 4.1.6 Test matrix

| Case | Expected |
|---|---|
| 2 shipments same group, same vehicle | arrivals equal, service charged once |
| 2 shipments same group, different vehicles | normal independent service |
| 3 shipments same group: 2 on V1, 1 on V2 | V1 charges once, V2 charges once |
| Group members with different `service` values | max service is charged |
| Group members at different locations (>1.1m) | problem rejected with 400 error |
| No `co_located_group` field on any shipment | behavior identical to mainline VROOM |
| 1 shipment with `co_located_group` | behavior identical to no-group |

#### 4.1.7 Acceptance criteria

- Consumer deletes `DispatchSharedPickupBatcher.php` (~460 LOC) and the
  peel-off loop in `VroomClient::solveIteratively` (~80 LOC). Tests remain
  green on a real day's dispatch data.
- Median solve time on 30-shipment problems with 50% co-located stays under 500 ms.
- `co_location_savings_seconds` matches PHP-computed expected savings on regression fixtures.

---

### F2 — Soft time windows

#### 4.2.1 Purpose

Today: time windows are hard. Our workaround `shiftRoutesLate` post-processes solutions
to push each route as late as possible within its window. This is uniform per route
and doesn't compose well when one route has a tight confirmed passenger and several
flexible pending ones.

Fork goal: let the solver natively prefer arrivals at or near a "preferred" time,
paying a linear cost per second of deviation.

#### 4.2.2 Input schema

Extend any `time_windows`-bearing step (pickup, delivery, break):

```json
"pickup": {
  "id": 42,
  "location": [25.34, 50.77],
  "time_windows": [[140100, 140700]],
  "soft_time_window": {
    "preferred": [140400, 140500],
    "cost_per_second_before": 0.1,
    "cost_per_second_after": 0.5
  }
}
```

- `time_windows` remains a hard constraint.
- `soft_time_window.preferred` is a single interval inside `time_windows`.
- `cost_per_second_before` and `_after` are floats ≥ 0. Scale is the same integer
  cost unit used elsewhere (compatible with `costs.fixed`).
- Field is optional. If absent, behavior unchanged.

#### 4.2.3 Semantics

For every step with a `soft_time_window`, the solver adds to the total cost:
```
if arrival < preferred.start:
  cost += cost_per_second_before × (preferred.start - arrival)
elif arrival > preferred.end:
  cost += cost_per_second_after × (arrival - preferred.end)
else:
  cost += 0
```

Arrival is not forced inside preferred; if the lowest-total-cost solution has the
arrival outside preferred, that's fine. The soft cost merely biases the search.

Edge cases:
- `preferred` MUST be a sub-interval of one of the hard `time_windows`. Rejected otherwise.
- Both `cost_per_second_before` and `cost_per_second_after` may be 0, making the window soft in name only.

#### 4.2.4 Output schema

Add to the step output:
```json
{ "type": "pickup", ..., "soft_window_violation_cost": 45 }
```
so the consumer can introspect which steps paid how much soft cost.

#### 4.2.5 Examples

Pending pickup, trip departs at 15:00 (= 140400 s with DAY_OFFSET):
- Hard window: `[[140100, 140400]]` (20 min before departure; arrival-by-departure cutoff)
- Soft preferred: `[140340, 140400]` (1 min before to dead-on)
- Cost before: 0.5, after: 1000

VROOM picks the earliest feasible time by default (~140100), paying
`0.5 × (140340 − 140100) = 120` in soft cost. With other costs in the thousands,
the solver prefers arrival later, ideally right inside `preferred`.

#### 4.2.6 Test matrix

| Case | Expected |
|---|---|
| No soft window set | behavior identical to mainline |
| Soft preferred fully inside hard window, arrival hits preferred | soft cost = 0 |
| Arrival before preferred start | linear cost applied |
| Arrival after preferred end | linear cost applied |
| Preferred interval outside hard window | problem rejected |
| Multiple hard windows, preferred inside one | correct |

#### 4.2.7 Acceptance criteria

- Consumer deletes `VroomClient::shiftRoutesLate` (~55 LOC).
- Routes with all-soft-pickups land as late as possible; routes with mixed
  tight/loose members land correctly constrained. Dispatcher UAT confirms.
- No more than 10% regression in median solve time.

---

### F3 — Per-objective cost breakdown

#### 4.3.1 Purpose

Today: `summary.cost: 12450` is opaque. Dispatcher UI can't explain "why this solution".

Fork goal: return a structured breakdown of which objectives contributed what to the total.

#### 4.3.2 Input schema

No input change.

#### 4.3.3 Output schema

Extend `summary`:
```json
"summary": {
  "cost": 12450,
  "cost_breakdown": {
    "distance": 8200,
    "duration": 3200,
    "fixed_vehicle": 800,
    "priority_bias": 0,
    "soft_time_window_violation": 250,
    "published_vehicle_deviation": 0
  },
  "routes_cost_breakdown": [
    {
      "vehicle_id": 1,
      "cost": 4200,
      "breakdown": { "distance": 2800, "duration": 1100, "fixed_vehicle": 300, ... }
    }
  ]
}
```

Each key in `cost_breakdown` sums across all routes; sum of all values equals `cost`
(within integer rounding). `routes_cost_breakdown` is per-vehicle.

#### 4.3.4 Semantics

During cost evaluation, VROOM already computes these components — they're just not exposed.
Implementation is accounting, not algorithmic.

#### 4.3.5 Acceptance criteria

- Sum of all breakdown components equals total cost (tolerate ≤ 1 unit integer drift).
- Consumer consumes the breakdown in a new dispatcher-UI panel ("Why this solution").
- No solve-time regression (< 5%).

---

### F4 — Plan diff endpoint

#### 4.4.1 Purpose

After any re-solve, dispatcher wants to see "what changed" — not re-read the whole plan.
Currently we'd compute this in PHP from two solution JSON blobs. Pushing it into the
fork is both faster and gives us a central place to format it.

Fork goal: a `POST /diff` endpoint that accepts two solutions and returns a structured diff.

#### 4.4.2 Endpoint

`POST /diff`

Request:
```json
{
  "before": { ...VROOM solution JSON... },
  "after":  { ...VROOM solution JSON... }
}
```

Response:
```json
{
  "shipment_diffs": [
    {
      "shipment_id": 42,
      "type": "moved_vehicle",
      "before": { "vehicle_id": 3, "arrival": 140100 },
      "after":  { "vehicle_id": 5, "arrival": 140700 }
    },
    {
      "shipment_id": 51,
      "type": "time_changed",
      "before": { "vehicle_id": 3, "arrival": 130000 },
      "after":  { "vehicle_id": 3, "arrival": 130500 }
    },
    {
      "shipment_id": 66,
      "type": "assigned_to_unassigned"
    },
    {
      "shipment_id": 99,
      "type": "unassigned_to_assigned",
      "after": { "vehicle_id": 4, "arrival": 150000 }
    },
    {
      "shipment_id": 100,
      "type": "unchanged"
    }
  ],
  "route_diffs": [
    {
      "vehicle_id": 3,
      "distance_change_km":   -2.3,
      "duration_change_seconds": -240,
      "shipment_count_change":  -1,
      "cost_change":            -450
    }
  ],
  "summary_diff": {
    "total_cost_change":      -450,
    "total_distance_change_m": -2300,
    "total_unassigned_change": -1
  }
}
```

#### 4.4.3 Semantics

- Shipments present in both solutions: classify as `moved_vehicle`, `time_changed`,
  or `unchanged`.
- Shipment `time_changed` threshold: `abs(after.arrival - before.arrival) > 60` seconds.
- Shipments only in `before`: `assigned_to_unassigned` (present in `before.routes`
  steps but not in `after.routes`) OR `removed_from_problem` (absent from `after` entirely).
- Shipments only in `after`: `unassigned_to_assigned` or `added_to_problem`.
- Route diffs include vehicles in either solution.

#### 4.4.4 Acceptance criteria

- Consumer uses `/diff` to drive a new dispatcher-UI panel showing a reshuffling summary
  before accepting a `Rebalance` action.
- Diff runs in < 50 ms for typical problem sizes.

---

### F5 — Richer unassigned-reason diagnostics

#### 4.5.1 Purpose

Today: `unassigned: [{id: 42, type: "pickup", location: [...], reason: "..."}]` —
`reason` is a free-text string from the solver's perspective. We pattern-match
it in `DispatchDiagnostician.php` (~260 LOC) to classify into user-facing hint codes.

Fork goal: emit structured, enumerated reason codes + machine-readable details.

#### 4.5.2 Input schema

No input change. Opt-in via query parameter `?diagnostics=full` to avoid overhead
on hot-path solves.

#### 4.5.3 Output schema

Replace the existing `reason` string with a structured object:

```json
"unassigned": [
  {
    "id": 42,
    "type": "pickup",
    "location": [25.34, 50.77],
    "reason": "time_window_infeasible",
    "details": {
      "pickup_earliest": 140100,
      "pickup_latest":   140400,
      "closest_feasible_vehicle": 3,
      "closest_feasible_arrival": 141000,
      "shortfall_seconds": 600
    }
  }
]
```

**Reason codes (enum)**:
- `no_vehicle_with_required_skills`
- `time_window_infeasible`
- `capacity_exceeded`
- `max_travel_time_exceeded`
- `route_duration_limit_exceeded`
- `no_feasible_insertion` (fallback)

**`details`** is a reason-code-specific object. Each reason code defines what fields
it provides:

- `no_vehicle_with_required_skills`: `{required_skills: [1, 2], vehicles_missing_skills: [1, 2, 3]}`
- `time_window_infeasible`: `{pickup_earliest, pickup_latest, closest_feasible_vehicle, closest_feasible_arrival, shortfall_seconds}`
- `capacity_exceeded`: `{dimension: "passengers", required: 3, max_available: 2}`
- `max_travel_time_exceeded`: `{max_allowed_seconds, would_require_seconds}`
- `route_duration_limit_exceeded`: `{max_allowed_seconds, would_require_seconds}`
- `no_feasible_insertion`: `{}` (no extra info — solver tried all insertions, none worked)

#### 4.5.4 Acceptance criteria

- Consumer simplifies `DispatchDiagnostician::classifyOne` from ~80 LOC to ~30 LOC
  (becomes a thin translator from reason code → user-facing hint).
- All existing diagnostic codes correctly map to one of the new reasons.
- Adding `?diagnostics=full` adds no more than 20% solve-time overhead.

---

## 5. P1 Feature specifications (ship if on schedule)

### F6 — Counterfactual mode

#### 5.6.1 Purpose

Dispatchers ask: "what if I added one more vehicle?", "what if I relaxed this
window by 10 min?". Today we'd run a second solve in the backend. Exposing a
first-class endpoint gives us atomic semantics, structured diffs, and caching.

#### 5.6.2 Endpoint

`POST /counterfactual`

Request:
```json
{
  "problem": { ...full VROOM problem JSON... },
  "what_if": {
    "add_vehicles":   [ { ...vehicle JSON... } ],
    "remove_vehicles": [ 3 ],
    "relax_time_windows": [
      { "shipment_id": 42, "step": "pickup", "delta_seconds": 600 }
    ],
    "add_shipments":   [ { ...shipment JSON... } ],
    "remove_shipments": [ 99 ]
  }
}
```

Response:
```json
{
  "baseline_solution": { ...full solution... },
  "modified_solution": { ...full solution... },
  "diff":              { ...same as /diff response... },
  "improvement": {
    "additional_assigned":   2,
    "cost_change":          -1200,
    "new_total_cost":       11250,
    "solve_time_ms_baseline":  180,
    "solve_time_ms_modified":  220
  }
}
```

Exactly one `what_if` transformation may be applied at a time (first one in the
enumeration order above wins; subsequent fields silently ignored). This is a
product decision — keeps the UX one-knob-at-a-time.

#### 5.6.3 Acceptance criteria

- Endpoint executes both solves in series; total wall-clock ≤ 2× single solve time.
- Consumer adds a dispatcher UI "what-if" dialog that uses this endpoint.

---

### F7 — Driver shifts and breaks

#### 5.7.1 Purpose

Real drivers have shifts with mandatory breaks. Today we model vehicles as available
all day (`time_window: [86400, 172800]`). Fleet scheduling happens out of band.

#### 5.7.2 Input schema

Extend vehicle:
```json
{
  "id": 1,
  "profile": "auto",
  "start": [...],
  "end":   [...],
  "capacity": [4, 3],
  "shifts": [
    { "start": 115200, "end": 140400 },
    { "start": 144000, "end": 158400 }
  ],
  "breaks": [
    {
      "id": 1,
      "time_windows": [[126000, 130500]],
      "service": 1800,
      "description": "lunch"
    }
  ]
}
```

`shifts` is an array of disjoint availability intervals. The existing `time_window`
field is preserved as a convenient single-shift alias. If both `time_window` and
`shifts` are set, `shifts` wins.

`breaks` is an array of break objects, each mirroring the OR-Tools `break`
concept: the break MUST be scheduled somewhere inside its `time_windows`, and the
solver decides where.

#### 5.7.3 Semantics

- Between shifts, the vehicle has capacity 0 and cannot drive (no travel allowed).
- Breaks consume route duration at the chosen time but do not involve travel.
- A break may fall between two stops; the second stop's arrival is pushed by
  `service` seconds.

#### 5.7.4 Acceptance criteria

- Problem with shifts/breaks solves correctly in test fixtures (e.g. fleet of 2
  drivers with overlapping shifts serves 20 shipments respecting break times).
- No regression for problems without shifts/breaks.

---

### F8 — Published-vehicle soft stability penalty

#### 5.8.1 Purpose

We use skills (F1 from the prior stability research) for HARD pinning. A soft
penalty is useful for passengers who are "confirmed but not locked" — we'd
prefer not to move them, but if the global plan is dramatically worse, moving
them is acceptable.

Fork goal: let any shipment declare a preferred vehicle with a per-deviation
cost penalty.

#### 5.8.2 Input schema

Extend shipment:
```json
{
  "id": 42,
  "pickup": { ... },
  "delivery": { ... },
  "published_vehicle": 3,
  "published_vehicle_cost": 500
}
```

- `published_vehicle` is an optional integer vehicle id.
- `published_vehicle_cost` is the cost added if the shipment ends up on a
  different vehicle.

#### 5.8.3 Semantics

During cost evaluation:
```
if assigned_vehicle != published_vehicle:
  cost += published_vehicle_cost
```

Straightforward additive cost term, applied once per shipment per solution.

#### 5.8.4 Risk

This feature requires a cost term that's consulted on every move evaluation.
Risk of solve-time regression if implemented naïvely. Profile before merging.

#### 5.8.5 Acceptance criteria

- Consumer uses this to express "pending but previously-suggested" status.
- Regression: under 15% median solve-time increase.

---

## 6. P2 / future work

Briefly documented so future contributors know what's been considered.

- **Re-optimization warm-start API** — `POST /reoptimize` accepting a previous
  solution + delta. Very high value but very high implementation risk; requires
  modifying the search seeding and possibly the metaheuristic itself.
- **Time-dependent travel matrix** — Valhalla time-buckets applied by arrival time.
  Probably 2× solve time cost. Marginal accuracy benefit in our geography.
- **Per-vehicle cost matrix** — each vehicle has its own distance/duration matrix.
  Useful for mixed fleets (electric + diesel + car).
- **Multi-trip vehicles** — a vehicle returns to depot and does another trip.
  Requires route structure changes (route = sequence of sub-routes).
- **Driver fairness** — variance-minimizing constraint on route durations.
  Academic work exists (Kovacs et al.), but non-additive objectives are hard in VROOM's
  search.

---

## 7. Delivery plan and milestones

| Milestone | Contents | Weeks | Deliverable |
|---|---|---|---|
| M0 | Fork created, CI green, docker image builds | 0.5 | `vroom-fork/main` passes upstream tests; pushes to our registry |
| M1 | F3 cost breakdown | 1 | Released tag `v1.x.0-busportal.m1`, consumer integration merged |
| M2 | F5 richer diagnostics | 1 | Tag `…m2`; consumer `DispatchDiagnostician` simplified |
| M3 | F1 co-located shared-stop | 2.5 | Tag `…m3`; consumer deletes `DispatchSharedPickupBatcher` + peel loop |
| M4 | F2 soft time windows | 1 | Tag `…m4`; consumer deletes `shiftRoutesLate` |
| M5 | F4 plan diff endpoint | 0.5 | Tag `…m5`; consumer consumes diff in UI |
| **P0 total** | | **~6.5 weeks** | All five P0 features live in dev |
| M6 | F6 counterfactual | 1 | Tag `…m6` |
| M7 | F7 shifts + breaks | 1.5 | Tag `…m7` |
| M8 | F8 published-vehicle penalty | 1 | Tag `…m8` |
| **P0 + P1 total** | | **~10 weeks** | Full fork roadmap complete |

Each milestone is:
- One upstream issue opened pre-dev ("Intent to implement X")
- Feature branch off our fork's `main`
- C++ unit tests + JSON fixture + docs patch
- Merged to our fork's `main` on green CI
- Tagged, dockerized, pushed to our registry
- Consumer update PR in `backend-dispatch`
- Dispatcher UAT on dev K8s
- After UAT green, optional PR to upstream VROOM

---

## 8. Non-functional requirements

### Performance
- **Median solve time** for our baseline fixture (30 shipments, 3 vehicles) MUST NOT
  exceed 600 ms after all P0 features merged (current: 500 ms).
- **P99 solve time** MUST NOT exceed 3 s (current with peel-off: ~2.5 s).
- **Memory** per solve MUST NOT exceed 2× current baseline.

### Compatibility
- Every new JSON field MUST be optional.
- Behavior MUST be identical to upstream VROOM when no new field is used.
- HTTP contract (request/response keys) is append-only; no renames, no removals.

### Code quality
- Follow upstream style (see `.clang-format`).
- C++20; match upstream's compiler-flag set.
- Every public-facing function MUST have a docstring.
- Every feature MUST have ≥80% line coverage on the new code (measured by gcov).

### Docs
- `docs/API.md` updated for every input/output schema change.
- Each feature gets a worked-example section.
- A `CHANGELOG.md` entry per milestone.

### Ops
- Docker image published to our GitLab registry per milestone.
- Tags semver-style: `v1.x.y-busportal.mN`.
- Health-check endpoint (`GET /health`) unchanged.
- Logs — stdout, same verbosity level as upstream.

---

## 9. Fork governance

### Branch model
- `main` — our production fork, always deployable. Lives at `github.com/gvoider/vroom`.
- `upstream/master` — remote tracking the upstream at `github.com/VROOM-Project/vroom`.
  Sync via `git fetch upstream && git rebase upstream/master` on `main` periodically.
- Feature branches: `feat/mN-description` — PR into `gvoider/vroom:main` for internal review
  before release tagging.

### Upstream contribution policy
- Before each feature, open an issue on upstream VROOM repo:
  "RFC: <feature>. We intend to implement this. Would a PR be welcome?"
- If upstream says yes, open a PR from a feature branch on `gvoider/vroom` to
  `VROOM-Project/vroom:master` after landing on our fork.
- If upstream says no or is silent, the feature stays on our fork only.
- Quarterly: rebase `main` onto `upstream/master`. Resolve conflicts. CI must stay green.

### Merge criteria
- CI green (upstream tests + our new tests)
- At least one code review (human or capable agent)
- Consumer integration smoke-tested on dev K8s

---

## 10. Integration points (consumer)

The `backend-dispatch` service consumes the fork via HTTP JSON at `http://vroom:3000/`
(or `http://vroom-fork:3000/` if deployed alongside for migration period).

Key consumer files to update per milestone (line numbers are as of commit
`93803bb` on `backend-dispatch` branch `dev`):

| Milestone | Files to update | Expected change |
|---|---|---|
| M1 (cost breakdown) | `VroomClient::solve` (+20 LOC) | parse `cost_breakdown` from response |
| M2 (diagnostics) | `DispatchDiagnostician::classifyOne` (−50 LOC) | switch to structured reason codes |
| M3 (co-located) | `DispatchSharedPickupBatcher.php` (DELETE ~460 LOC), `VroomClient::solveIteratively` (−80 LOC) | send `co_located_group` on each pickup; drop batcher entirely; drop peel loop |
| M4 (soft windows) | `VroomClient::shiftRoutesLate` (DELETE ~55 LOC) | send `soft_time_window` on pending pickups; drop post-processor |
| M5 (diff) | New file `DispatchPlanDiffer.php` (+50 LOC) | call `/diff` from re-solve flow |
| M6 (counterfactual) | New endpoint in `DispatchController` | wire dispatcher "what-if" UI |
| M7 (shifts/breaks) | `VroomClient::buildVroomVehicles` (+30 LOC) | emit shifts/breaks per vehicle |
| M8 (published-vehicle) | `VroomClient::assembleProblem` (+15 LOC) | emit `published_vehicle` on soft-pinned shipments |

**Total consumer code delta after all P0**: **−495 LOC net**.

---

## 11. Success criteria

### Quantitative
- **Consumer LOC reduction**: ≥450 lines deleted from `backend-dispatch/src/Dispatch/`.
- **Solve performance**: median ≤ 600 ms on 30-shipment baseline, P99 ≤ 3 s.
- **Test coverage**: ≥ 80% on new code.
- **Integration test pass rate**: 100% of current `backend-dispatch` dispatcher-workflow
  integration tests pass on the fork.

### Qualitative
- Dispatcher reports no "shuffle chaos" across re-solves (the original pain).
- Dispatcher reports seeing "why this solution" breakdowns in UI and finding them useful.
- Dispatcher uses "what-if" to explore decisions before committing.

### Operational
- Fork ships with the same single-container Docker deployment model as upstream.
- K8s manifests in `bus-vdexpress/vroom-fork-deploy.yaml` deploy cleanly.
- Rollback plan: revert consumer's integration commit; the old VROOM pod is still
  around during migration.

---

## 12. Risks and mitigations

| Risk | Probability | Impact | Mitigation |
|---|---|---|---|
| F1 (co-located) requires touching solver inner loop; introduces bugs | Medium | High | Extensive regression-test fixtures; A/B compare solutions against upstream on non-co-located inputs |
| F2 (soft time windows) causes solve-time regression >10% | Medium | Medium | Profile before merge; if necessary, gate behind a flag so only pending pickups get soft windows |
| F8 (published-vehicle) requires per-move cost eval, slowing solver | Medium | Medium | Implement as additive post-hoc cost if possible; defer to P2 if solve regression > 20% |
| Upstream rejects all our PRs → we own the fork forever | High | Low | We already assume the worst case: the fork exists and we maintain it regardless |
| Engineer gets stuck on a milestone for > 2× estimate | Medium | High | Time-box each milestone. If over 2× estimate, pivot to a migration or a scaled-down feature. |
| Consumer tests break during a milestone | Low | Medium | Fork CI runs the consumer's integration test suite on each fork release candidate |

---

## 13. Appendices

### A. Reference files in this repo

- `ref/reports/vroom-workarounds-inventory.md` — authoritative list of 33 workarounds
  the fork aims to replace or simplify.
- `ref/reports/vroom-alternatives-research.md` — why we chose fork over Timefold/OR-Tools.
- `ref/reports/dispatch-stability-research.md` — the "chaotic shuffle" complaint and
  its industry-standard solutions.
- `backend-dispatch/src/Dispatch/` — consumer code. Start with `VroomClient.php` and
  `DispatchSharedPickupBatcher.php`.

### B. Useful external links

- VROOM repository: https://github.com/VROOM-Project/vroom
- VROOM API docs: https://github.com/VROOM-Project/vroom/blob/master/docs/API.md
- VROOM issue #800 "Allow defining immutable tasks": https://github.com/VROOM-Project/vroom/issues/800
- vroom-express (HTTP server): https://github.com/VROOM-Project/vroom-express
- Valhalla docs (router backend): https://valhalla.github.io/valhalla/
- Upstream VROOM contribution guide: https://github.com/VROOM-Project/vroom/blob/master/CONTRIBUTING.md

### C. Command-line quick reference

```bash
# Clone our fork and track upstream
git clone https://github.com/gvoider/vroom.git
cd vroom
git remote add upstream https://github.com/VROOM-Project/vroom.git
git fetch upstream

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Test
cd ..
./src/vroom -i tests/fixtures/example_1.json

# Bench (after M0 scaffolding)
./scripts/bench-busportal.sh tests/fixtures/busportal-30-shipments-3-vehicles.json

# Docker build
docker build -t registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork:dev .

# Deploy to dev K8s
kubectl --context dev -n bus-vdexpress apply -f k8s/vroom-fork-deploy.yaml
```

### D. Contact

- **Product owner for this fork**: cyril@biryulov.net (project lead)
- **Consumer integration**: `backend-dispatch` maintainers
- **Ops**: K8s manifests in `bus-vdexpress` namespace, both `dev` and `prod2` contexts

---

**End of document.** Next step: engineer opens this, reads §0 and §1, then proceeds to M0.
