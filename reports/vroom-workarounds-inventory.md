# Backend-Dispatch — VROOM Workarounds & Hacks Inventory

**Date**: 2026-04-18
**Scope**: `backend-dispatch/src/Dispatch/` — all solver-adjacent code
**Purpose**: ground-truth input for the Timefold migration re-estimate

This is an exhaustive inventory of every place `backend-dispatch` bends, wraps, pads,
pre-processes, or post-processes VROOM's output — either because VROOM doesn't support
what we need, or because its native behavior produces results our dispatchers reject.

---

## A. Time-model hacks (VROOM is second-based; our domain is calendar-based)

### A1. DAY_OFFSET = 86400 synthetic epoch
**Where**: `VroomClient.php:29`, referenced throughout
**What**: All times are expressed as "seconds from PREVIOUS day midnight, stop-local".
Current-day midnight sits at 86400; current-day 08:00 at 115200; the next midnight at 172800.
**Why**: VROOM disallows negative times. A 23:00 pickup for an 02:00 trip must be positive;
using true midnight would either clip it or flip sign.
**Fragility**: the `86400` constant appears in vehicle `time_window`, in every shipment's
`pickup_earliest/latest` and `delivery_earliest/latest`, and the reverse-mapping
`vroomSecondsToClockTime()` must strip it. One off-by-one and routes vanish.

### A2. Service time forced to 0
**Where**: `VroomClient.php:419`
**What**: Overrides `$pickupServiceTime = 0` regardless of what the caller passed or
what the pickup scope (global vs local virtual stop) would have demanded (5 min / 10 min).
**Why**: passengers at the same coordinates were being scheduled 3–10 min apart because
VROOM was budgeting dwell time between them. We needed collapse to a single clock time.
**Fragility**: legitimate "real dwell" is now invisible to the solver. If a driver truly
takes 5 min to load luggage, routes will silently run late.

### A3. Tight ±300 s windows for confirmed pickups
**Where**: `VroomClient.php:425-427`
**What**: A confirmed request gets `pickup_earliest = confirmedSec - 300`,
`pickup_latest = confirmedSec + 300`.
**Why**: to lock the time in place across re-solves. Combined with `priority: 100`,
it's our "don't change this passenger's time" protocol.
**Fragility**: doesn't lock the *vehicle*. VROOM may reassign a confirmed passenger to a
different vehicle as long as the new one meets the ±5 min window.

### A4. Wide pending windows anchored to trip departure
**Where**: `VroomClient.php:418-419, 429-430`
**What**: `deliveryLatest = tripDeparture - 30 min`; `pickupLatest = deliveryLatest`;
`deliveryEarliest = tripDeparture - 3 h`; `pickupEarliest = deliveryEarliest - 4 h`.
**Why**: we have no hard customer pickup time for pending requests; we only know the
trip they need to catch. The 30 min buffer before departure is our SLA.
**Fragility**: the 30/180/240/… minute magic numbers are scattered across three lines,
undocumented as policy anywhere. Changing SLA means hunting constants.

### A5. `shiftRoutesLate()` — push routes to the right
**Where**: `VroomClient.php:564-618`
**What**: After VROOM solves, computes per-route slack (`min(windowEnd - arrival)`)
and uniformly shifts every step later by that slack.
**Why**: VROOM minimizes cost, not makespan; it schedules everything at earliest
feasible time. Passengers don't want to stand at a pickup an hour early — they want
the pickup as close to their trip as possible.
**Fragility**: shift is uniform across one route. A mixed route with one tight-window
confirmed passenger early and a flexible pending passenger late will have no slack.
Does nothing if `shipmentWindows[shipmentId]` is missing (silent no-op).

### A6. Pickup-time rounding to 5-min boundary
**Where**: `VroomClient.php:747-754` (helper) and callsites at lines 782 & 807
**What**: `vroomSecondsToClockTime(..., PICKUP_ROUND_MINUTES=5)` ceil-rounds extracted
pickup times up to the next 5-min mark.
**Why**: human-friendly times (14:05 not 14:03) — matches spoken dispatcher language.
**Fragility**: rounds display time but not the VROOM-internal arrival. Downstream
consumers see 14:05, driver app may see 14:03 if it reads elsewhere.

---

## B. Speed / distance model hacks

### B1. Vehicle `speed_factor: 0.85`
**Where**: `VroomClient.php:190`
**What**: tells VROOM that every vehicle moves at 85% of its Valhalla-computed speed,
inflating all travel times by ~17.6%.
**Why**: Valhalla's `auto` costing doesn't model traffic lights, stops, urban drag,
driver hesitation. Real route times were consistently 10–20% longer than computed.
**Fragility**: one magic number; not tuned per-road-class or time-of-day. A shift to
highway-heavy routes would over-inflate; a shift to dense-urban would under-inflate.

### B2. High fixed vehicle cost (100,000 units)
**Where**: `VroomClient.php:202`
**What**: each vehicle carries a `costs.fixed: 100000` so VROOM prefers filling one
vehicle over splitting across two.
**Why**: absent this, VROOM happily spawns a separate route for one passenger.
**Fragility**: arbitrary number that must dominate any plausible per-km-× or
per-minute cost VROOM computes internally. If Valhalla matrix ever returns cost in
different units, the comparison inverts silently.

### B3. Trunk volume → integer luggage units (volume ÷ 100)
**Where**: `VroomClient.php:186`; mirrored in `DispatchSharedPickupBatcher.php`
**What**: `trunkVolumeLiters` (e.g. 400) → integer `luggageUnits` (4). Passenger luggage
count is then compared against integer capacity.
**Why**: VROOM capacity dimensions are integers; our vehicles store liters.
**Fragility**: quantization. A 399-liter trunk rounds to 3, not 4. A vehicle with
401 L and another with 499 L are treated identically at capacity=4.

---

## C. Vehicle-positioning hacks

### C1. Free-floating vehicles (no `start`)
**Where**: `VroomClient.php:192-197`
**What**: vehicles have `end` (station) but no `start`. VROOM treats them as
"materialize at first pickup at earliest feasible time".
**Why**: with a `start`, VROOM bills the solver for a 2–3 h drive from a Lviv depot
to the first rural pickup, which collapses the delivery-window feasibility budget.
**Fragility**: real vehicles *do* start somewhere. The solver's "this took 1 h"
answer has a hidden 2 h cost invisible to it. Route economics compare apples to
oranges. Real-world positioning is "handled outside the solver" — i.e. the
dispatcher just figures it out manually.

### C2. Vehicle `time_window` is always 00:00–23:59 today
**Where**: `VroomClient.php:200`
**What**: every vehicle is told it's available the whole service day.
**Why**: we don't model driver shifts or breaks in the problem.
**Fragility**: no driver break, no split shift, no lunch. Fleet scheduling has to
happen at a layer above.

---

## D. Shared-stop batching — ~460 LoC of custom algorithm

### D1. Branch-and-bound minimum-batch partition
**Where**: `DispatchSharedPickupBatcher.php::findBestPartition` (lines 82-141)
**What**: for ≤10 requests at one stop, enumerate all partitions into feasible
(windows intersect, capacity fits) batches, pick the one with fewest batches.
Uses a batch-signature dedup to prune symmetric states.
**Why**: VROOM has no concept of "these shipments should be picked up together".
Without batching, N passengers at a shared stop become N separate shipments that
VROOM may route through different vehicles, arriving minutes apart.
**Fragility**: pure CPU work on the PHP side. Duplicates VROOM's own combinatorial
search, upstream of the solver. Bell-number blowup beyond 10 triggers a fallback.

### D2. Greedy first-fit-decreasing fallback for >10 requests
**Where**: `DispatchSharedPickupBatcher.php::greedyPartition` (lines 346-388)
**What**: sort by passenger count desc, insert each into first batch that accepts.
**Why**: branch-and-bound is infeasible for >10.
**Fragility**: produces *a* valid partition, not the minimum one. The quality cliff
at request #11 is invisible and undocumented to dispatchers.

### D3. Iterative peel-off loop
**Where**: `VroomClient.php::solveIteratively` (lines 92-173)
**What**: up to 5 outer iterations. If VROOM can't assign a batch, remove its
least-anchored member (pending before confirmed, lowest luggage, widest window,
highest requestId) and re-solve.
**Why**: a batch might be individually fine but not fit any route together. Peeling
one member either shrinks it or dissolves it entirely.
**Fragility**: 5 iterations = 5 full VROOM calls. P99 latency balloons. Each
iteration is ~100–500 ms. Priority rules for what-to-peel are a hand-tuned policy
that has never been A/B tested.

### D4. Synthetic shipment IDs for merged batches
**Where**: `VroomClient.php::nextSyntheticShipmentId` (line 540) +
`assembleProblem` (lines 278-307)
**What**: batched shipments get an ID > any real requestId, to distinguish them
from solo shipments that use requestId directly as shipmentId.
**Why**: VROOM shipments need unique integer IDs; we need to reverse-map back to
requestIds after solve.
**Fragility**: the `shipmentRequestIds` + `shipmentIdToBatchLeader` maps must be
passed through every downstream function (extractCallOrder, extractPickupTimes,
extractRoutePickups, diagnose, …). Forget to pass them and results silently wrong.

### D5. Time-window intersection for batches
**Where**: `DispatchSharedPickupBatcher.php::tryAddToBatch` (lines 186-221)
**What**: merged batch pickup window = max(earliest) ∩ min(latest); same for
delivery. If intersection empty, merge rejected.
**Why**: a batch must serve every member's constraint.
**Fragility**: a tight-window confirmed passenger merged with a flexible pending
passenger produces a tight batch window; the pending passenger loses their
flexibility. Intentional but not obvious.

### D6. Coordinate-based fallback sharedStopKey (just added today)
**Where**: `VroomClient.php:468-473`
**What**: if a request has no `virtualStopId`, group by `sprintf('coord:%.5f,%.5f')`
so two door-pickups at the same coords still batch.
**Why**: previously the batcher only grouped on `virtualStopId`; our dispatcher
recreated the same physical stop by dropping a pin, and they came out unbatched.
**Fragility**: false positives at 5 decimals (~1.1 m) are unlikely but not zero.
Two distinct apartments at a shared entrance coord would merge.

---

## E. Priority-based pseudo-constraints

### E1. Three-tier priority 100 / 50 / 10
**Where**: `VroomClient.php::computeContextPriority` (lines 358-368)
**What**: 100 = confirmed, 50 = tentative (the request the dispatcher is currently
editing), 10 = other pending.
**Why**: VROOM lacks "lock this shipment" / "prefer this shipment". Priority is the
closest primitive. 100 means "try really hard"; 50 biases tentative placement; 10
is business-as-usual.
**Fragility**: priority is a *cost modifier*, not a constraint. VROOM may still
drop a priority-100 shipment if infeasible. We conflate "time-locked" and
"protected" with one primitive. Most critically: **no vehicle pinning** — a
priority-100 confirmed passenger can still be reshuffled across vehicles between
solves. This is the "chaotic shuffle" complaint we're trying to fix.

### E2. Batch priority = max of member priorities
**Where**: `VroomClient.php::computeBatchPriority` (lines 375-385)
**What**: a batch with any confirmed member inherits 100; a batch with the
tentative-edit member inherits 50; else 10.
**Why**: a batch containing a confirmed pickup must be protected as a whole unit,
because breaking it up would unlock a confirmed pickup.
**Fragility**: correct but brittle. If the priority scheme in E1 ever gains a new
tier, this max-pick breaks.

---

## F. Pre-solve enrichment (domain → VROOM)

### F1. Transient passenger overrides
**Where**: `DispatchController.php::optimizeDay` lines 1123-1158
**What**: `payload['passengerOverrides'][requestId]` may contain `pickupLat/Lng/Text`,
`virtualStopId`, `confirmedPickupTime`. These are *applied to the in-memory request
objects before solve*, never persisted.
**Why**: dispatcher UI lets the user drag pins and preview changes without
committing to DB. We solve against the hypothetical state.
**Fragility**: every solve has different "truth". If two dispatchers edit the same
day, optimistic-UI consistency depends on clocking through a solver that has no
idea.

### F2. Auto-assignment of nearby virtual stops
**Where**: `DispatchController.php::buildAutoAssignedLocationOverrides` (lines 1270-1365)
**What**: for each still-pending passenger without a virtualStop, looks for
local (0.5 km) and global (5 km) virtual stops. Keeps only stops that serve ≥2
passengers. Picks the stop that maximizes member count, then global>local, then
shortest total walk.
**Why**: preemptively collapse scattered pickups onto shared stops before the
solver sees them. Reduces shipment count, helps batching.
**Fragility**: ~95 lines of custom clustering before the solver even runs. Assigns
silently — the dispatcher doesn't see "we moved your passenger to stop X" until
the result comes back.

### F3. `resolvePickupData` — three-source priority lookup
**Where**: `VroomClient.php:472-517`
**What**: pickup coords source priority: (1) override map → (2) request's saved
virtualStopAddress → (3) raw pickupAddress. Each can be JSON-encoded string or
stdClass or array.
**Why**: the location field is stored different ways depending on how the request
was created. Unifying is deferred to solve-time.
**Fragility**: a silent lookup miss falls back to raw address, which may be
door-pickup — subtle behavior change vs intended virtual stop.

---

## G. Post-solve enrichment (VROOM → domain)

### G1. Farthest-first call-order extraction
**Where**: `VroomClient.php::extractCallOrder` (lines 701-725)
**What**: for each route, reverse pickup order so the farthest-from-station
passenger is the first to call.
**Why**: dispatcher calls passengers in driving order so each one is ready by the
time the driver reaches them. Farthest called first = more lead time.
**Fragility**: assumes the route is monotonic-distance from station, which is only
true on average.

### G2. Virtual stop suggestion engine (3-strategy)
**Where**: `VirtualStopSuggester.php` (full file, ~300 LoC)
**What**: after each solve, proposes (a) merges of ≥2 nearby passengers within
500m walk, (b) snaps of single passengers to route polyline if ≥500m detour saved,
(c) predefined stops within 500m (local) or 5km (global). Always appends a
door-to-door fallback.
**Why**: dispatcher sees "try moving passenger X to shared stop Y — saves 4 km".
**Fragility**: the snap strategy walks VROOM's geometry polyline; if VROOM ever
changes geometry encoding, breaks silently. MERGE/SNAP/WALK thresholds are magic
constants (500 m walk, 1 km merge radius, 500 m saved-detour minimum).

### G3. DispatchDiagnostician — classify unassigned shipments
**Where**: `DispatchDiagnostician.php` (~260 LoC)
**What**: VROOM says "unassigned: [7, 12, 15]" with minimal reason. Diagnostician
classifies each into NO_VEHICLE, PEELED_FROM_BATCH, CAPACITY_EXCEEDED,
TIME_WINDOW_TIGHT, UNKNOWN, plus action hints.
**Why**: VROOM's native unassigned-reason is opaque. Dispatchers need
"why?" not "rejected".
**Fragility**: pattern-matches on inputs (peelLog, window tightness threshold,
total capacity). A new infeasibility class silently becomes UNKNOWN.

### G4. DispatchEconomics — compare solver cost vs taxi baseline
**Where**: `DispatchEconomics.php` (~70 LoC)
**What**: computes our marginal cost/km + driver hourly vs external-taxi baseline
(base fare + per-km). Emits "cheaper by X%" hint.
**Why**: decides whether to escalate a problem pickup to an external taxi.
**Fragility**: outside the solver entirely. Could be inside as an objective if
solver allowed it.

### G5. Route timeline extraction
**Where**: `VroomClient.php::extractTimeline` (lines 871-910)
**What**: per route, extract {departure, firstPickupTime, stationArrivalTime,
durationMinutes}. Departure = vehicle start step; station-arrival = last delivery
OR end step.
**Why**: UI shows "Vehicle 3: departs 07:30, first pickup 09:45, arrives at
station 11:15".
**Fragility**: depends on VROOM always emitting `start`/`end`/`pickup`/`delivery`
step types. No `waypoint` or custom step support.

### G6. Synthetic-ID-aware pickup time extraction
**Where**: `VroomClient.php::extractPickupTimes` (780-795) and
`extractRoutePickups` (lines 803-814)
**What**: for every `pickup` step, expand the shipment ID back to 1–N request IDs
via `shipmentRequestIds` map; assign every expanded request the same arrival time.
**Why**: the batching in D4 stored one shipment for multiple passengers; we need
per-request times in the UI.
**Fragility**: if the map wasn't passed through, it defaults to identity mapping;
the multi-passenger batched times silently collapse to the leader only.

---

## H. Out-of-band features (not modelled in the solver at all)

### H1. Multi-trip day optimization
**Where**: `DispatchController::optimizeDay` (lines 1021-1268)
**What**: iterates `trips` for a date, syncs DispatchRequests for each trip, pools
all requests into one VROOM problem. Vehicles are shared across trips.
**Why**: one vehicle can serve a 08:00 trip and a 14:00 trip the same day if
timings allow.
**Fragility**: vehicle-reuse constraints (driver breaks, refueling, DSR) are
absent. Solver freely reassigns vehicles between trips.

### H2. Rolling-horizon re-solve on every change
**Where**: called from admin-dispatch frontend; no specific backend-dispatch line
**What**: every UI action (add pax, move pin, confirm, cancel) triggers a full
optimizeDay call.
**Why**: no incremental API.
**Fragility**: every re-solve is from scratch. With the 5-iteration peel loop,
interactive latency is noticeable. And — this is the reshuffling complaint —
nothing prevents VROOM from completely re-optimizing across confirmed passengers.

### H3. Auto-confirm on tentative
**Where**: implied in `optimizeDay`; dispatcher UI sends `tentativeConfirmId`
**What**: a request being edited gets priority 50 so VROOM biases toward placing
it. If it survives the solve at an acceptable time, UI offers "confirm".
**Fragility**: the confirm decision is made one-shot on the solve result; if the
next re-solve would have rejected it, the dispatcher already clicked confirm.

### H4. Escalation to external taxi
**Where**: `DispatchController::escalate` (lines 1602-1626), `DispatchEscalation`
record
**What**: a request that can't be served internally gets routed to an external
taxi provider via a DB record.
**Why**: last-mile capacity constraint.
**Fragility**: external state is not in the solver's view. The solver might
accept a request whose optimal slot was left open for a since-escalated
passenger.

### H5. Group management
**Where**: `DispatchGroup` + `createGroup`, `optimizeGroup`, `confirmGroup`
endpoints
**What**: dispatcher can define "this set of 4 pending requests must be handled
together" as a group, then optimize/confirm as a unit.
**Why**: domain pressure (families, wheelchair + companion) that the solver
doesn't model.
**Fragility**: groups are expressed as VROOM batches (same machinery as D1-D5).
Priority and identity rules layered on top.

---

## Summary — surface area in numbers

| Category | Hack count | Approx LoC | Critical? |
|---|---|---|---|
| A. Time model | 6 | ~150 | Yes (all of them) |
| B. Speed / distance | 3 | ~15 | Yes (B1, B2) |
| C. Vehicle positioning | 2 | ~10 | Yes (C1) |
| D. Shared-stop batching | 6 | ~460 | Yes (all) |
| E. Priority pseudo-constraints | 2 | ~30 | Yes (E1) |
| F. Pre-solve enrichment | 3 | ~150 | Mostly (F1, F2) |
| G. Post-solve enrichment | 6 | ~500 | Partially |
| H. Out-of-band features | 5 | ~500 | Yes (H2, H4, H5) |
| **Total** | **33** | **~1815** | |

About **1800 LOC of solver-adjacent logic** — most of it compensating for
primitives VROOM doesn't expose (no pin, no soft cost, no re-opt API, no
diagnostics, no incremental change). The solver itself is a ~200-char JSON POST.

---

## Cross-cut: what a "move off VROOM" migration actually means

| Hack class | Port? | Timefold-native replacement |
|---|---|---|
| A1 DAY_OFFSET | Drop | Timefold uses `LocalDateTime`, not second-offsets |
| A2 service=0 | Drop | Set `Visit.serviceDuration = Duration.ZERO`; or compute from shared-stop rule |
| A3 ±5min | Keep | `TimeWindowConstraint` with publishedValue bias |
| A4 Wide pending windows | Port | Plain `TimeWindow` entity |
| A5 shiftRoutesLate | Drop | Timefold supports late-start objectives natively |
| A6 round-up 5min | Keep (display only) | Unchanged |
| B1 speed_factor | Keep | Router matrix multiplier (or inflate at matrix layer) |
| B2 vehicle fixed cost | Keep | Soft constraint weight |
| B3 trunk-volume rounding | Port | `@PlanningVariable` capacity int |
| C1 free-floating | Port | Timefold vehicles can have null start |
| C2 time_window all-day | Port | Shift entity with driver breaks (new feature) |
| D1–D5 batching | **Drop entirely** | Timefold supports "visits that must be co-located" via chained entities / `@ShadowVariable` — no pre-processing, the solver figures it out |
| D3 peel-off loop | **Drop entirely** | Timefold's `PartialAssignmentPolicy` handles infeasibility natively |
| D6 coord-based key | **Drop** | Same as D1-D5 |
| E1 priority 100/50/10 | **Replace** | `@PlanningPin` (hard) + `publishedValue` penalty (soft) — this is the main prize |
| F1 transient overrides | Port | Same pattern, call `ProblemChange` instead of full re-solve |
| F2 auto-assign virtualStop | Keep | Pre-solve heuristic; move into construction phase |
| G1 farthest-first | Keep | Display-layer extraction |
| G2 VirtualStopSuggester | Keep (or enhance) | Post-solve heuristic; independent |
| G3 DispatchDiagnostician | **Upgrade massively** | Timefold's `ScoreExplanation` replaces this + gives per-constraint attribution |
| G4 DispatchEconomics | Keep | Business layer, unchanged |
| G5 timeline extraction | Port | Read from `VehicleRoute` entity |
| G6 synthetic-ID expand | **Drop entirely** | No batching → no synthetic IDs |
| H1 multi-trip | Port | Timefold handles this as-is |
| H2 rolling-horizon | **Replace** | `ProblemChange` = the answer |
| H3 auto-confirm | Port | Unchanged |
| H4 escalation | Port | Unchanged |
| H5 groups | Port | Constraint: "these visits share a vehicle" |

**What gets DROPPED**: D1 (~140 LoC B&B), D2 (~40 LoC greedy), D3 (~80 LoC peel
loop), D4+D6 (~60 LoC synthetic-ID + coord-key plumbing), G6 (~30 LoC expand),
A1 (~50 LoC epoch arithmetic sprinkled everywhere), A5 (~55 LoC shiftRoutesLate).

That's about **455 LoC of solver-adjacency code** that disappears because
Timefold models these as first-class concepts. Plus ~2 weeks of dispatcher-trust
debt (priorities-as-fake-pinning).

**What gets KEPT**: A3, A4, A6, B1, B2, B3, C1, C2, F1, F2, G1, G2, G4, G5,
H1, H3, H4, H5 — all the *domain* logic. About 1150 LoC.

**What gets UPGRADED**: E1 → real pinning + penalty (win), G3 → score explanation
(win), H2 → incremental re-opt (win).

**What gets NEW WORK**: the Timefold *domain model* itself — probably 600–900
LoC of Java/Kotlin for the paired-shipment DARP, constraint streams, score config.
