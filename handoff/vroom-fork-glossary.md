# VROOM Fork — Domain Glossary

Terms that appear throughout the RFC and supporting docs. If you (the engineer picking this up) encounter an unfamiliar term while reading code in `backend-dispatch` or the research reports, check here first.

---

## Dispatch domain

**Dispatcher** — the human operator using our admin UI to assemble a daily plan: call passengers, confirm pickups, assign vehicles, handle last-minute changes. NOT a driver. A single person covering ~1–3 daily trips and their transfer passengers.

**Trip** — a scheduled bus departure (e.g. "13:00 Lviv → Kyiv"). Has a fixed departure time and a fixed origin city. Trips do not move; vehicles move.

**DispatchRequest** — a passenger's ask for pickup before a specific trip. One row in the `dispatch_requests` table. Has a status lifecycle: `pending` → `confirmed` (committed pickup time) → optionally `cancelled`.

**Pending request** — dispatcher hasn't committed a pickup time yet. The solver has flexibility to place it anywhere in a wide window (typically 3 h before trip to 30 min before trip).

**Confirmed request** — the dispatcher has committed to a specific pickup time for this passenger. Stored as `confirmedPickupTime`. The solver MUST honor this (± 5 min tolerance for small adjustments). **Re-optimization must not change a confirmed passenger's time.** A vehicle may still change unless we pin it (that's what the fork's F1 skills-hack and F8 published_vehicle solve).

**Tentative-confirm** — the dispatcher is currently editing a pending request in the UI — hasn't saved yet but wants to preview "would this fit?". Passed to the solver as `tentativeConfirmId`. Solver gives this shipment `priority: 50` (vs 100 confirmed, 10 other pending) to bias toward placing it.

**Virtual stop** — a named pickup point that multiple passengers can share ("Kolonka Gas Station corner"). Stored in `dispatch_virtual_stops`. Has a `scope`: `LOCAL` (500 m walking radius, 1 stop per neighborhood) or `GLOBAL` (5 km, 1 stop per town — for small towns en route).

**Door pickup** — pickup at the passenger's own address (lat/lng from their booking). No virtual stop involved. Usually for pax who can't walk to a virtual stop.

**Shared stop** — informal term for "multiple passengers at the same virtual stop in one day". The solver SHOULD pick them up together on one vehicle at one arrival time. Today handled by our `DispatchSharedPickupBatcher`; the fork's F1 makes this native.

---

## VROOM domain

**Problem JSON** — the input we POST to VROOM. Schema: top-level `{vehicles, shipments, options}`. See fixtures in `vroom-fork-fixtures/`.

**Shipment** — one pickup→delivery pair in VROOM's vocabulary. A pickup step at some coordinates, a delivery step at the bus station. Both MUST be served by the same vehicle.

**Vehicle** — a solver-visible resource. Has capacity (2-D in our case: passengers + luggage units), a time window, costs. Our vehicles have `end` (station) but no `start` — they're free-floating per RFC/workarounds discussion.

**Route** — VROOM's output per vehicle: an ordered sequence of `steps` (start? → pickup → delivery → pickup → … → end).

**Step** — one event in a route. Types: `start`, `pickup`, `delivery`, `end`. Has `arrival` (seconds from day-offset), `service` (dwell), optional `departure`.

**Unassigned** — a shipment the solver couldn't place in any route. Returned with a `reason` string (coarse today, structured enum post-M2).

**Priority** — integer 0–100, per shipment. Cost bias, not a hard constraint: the solver prefers to place high-priority shipments but may still drop them if infeasible.

**Skills** — array of integers per shipment/vehicle. Hard constraint: a shipment can only go on vehicles whose skills array ⊇ the shipment's skills. Our Tier-0 pinning uses this by giving each confirmed shipment a unique skill that only its assigned vehicle has.

**Time window** — `[earliest, latest]` in VROOM seconds. Hard constraint on arrival. Per step; a step may have multiple disjoint windows.

**Matrix** — the N×N Valhalla-computed travel-time-and-distance lookup between all locations in the problem. VROOM fetches this at solve time via its configured router.

**Valhalla** — the routing engine we use. Open-source, tile-based (OpenStreetMap). Runs as a separate service in our cluster at `valhalla:8002`. VROOM calls its `/sources_to_targets` endpoint.

**Profile** — Valhalla costing mode: `auto` (car), `bus`, `truck`, `bicycle`, `pedestrian`, etc. We always use `auto` in this project.

**`speed_factor`** — per-vehicle multiplier on travel times from the matrix. We set `0.85` to pad Valhalla's optimistic estimates by ~17.6%. Upstream feature, not a fork addition.

---

## Our code's vocabulary

**DAY_OFFSET** (86400) — the synthetic epoch we use: all VROOM times are seconds from "previous day midnight in the stop's local timezone". Today-midnight = 86400; today-08:00 = 115200; today-noon = 129600; today-23:59 = 172800. Needed because VROOM disallows negative times and we have cross-midnight pickups (a 23:30 pickup for an 02:00 trip).

**sharedStopKey** — our internal batching key. String like `"stop:42"` if `virtualStopId` is set, or `"coord:50.76658,25.34360"` fallback (5-decimal rounding ≈ 1.1 m) otherwise. Two requests with the same key are candidates for batching into one VROOM shipment.

**Peel-off loop** — our iterative solve retry. If VROOM can't place a batched shipment, we remove (peel) the lowest-anchored member and re-solve. Up to 5 iterations. Goes away post-F1.

**Synthetic shipment ID** — when we merge N requests into one batched shipment, we invent a shipment ID greater than any real requestId. Reverse-map via `shipmentRequestIds`. Goes away post-F1.

**`shiftRoutesLate`** — our post-processing that pushes each route's timings as late as possible within its windows. Compensates for VROOM's earliest-feasible default. Goes away post-F2 (soft time windows).

**Published vehicle** — a shipment's *previously assigned* vehicle from the last solve. If this solve assigns them elsewhere, we want to penalize the move. Currently not tracked — F8 introduces the primitive.

**Virtual stop auto-assign** — our `buildAutoAssignedLocationOverrides` preprocessing: when a pending request lacks a virtualStop, we look for a nearby one that's also useful for ≥2 other pending requests and auto-assign. Stays as-is post-fork; doesn't depend on any solver primitive.

---

## Acronyms

| Acronym | Expansion |
|---|---|
| DARP | Dial-a-Ride Problem (the academic name for our class of problem) |
| VRP | Vehicle Routing Problem (parent class) |
| PDPTW | Pickup-and-Delivery Problem with Time Windows |
| CVRP | Capacitated VRP |
| ConVRP | Consistent VRP (the "don't reshuffle across solves" literature) |
| ALNS | Adaptive Large Neighborhood Search (a metaheuristic) |
| HGS | Hybrid Genetic Search (another metaheuristic; powers PyVRP) |
| DRT | Demand-Responsive Transport |
| NEMT | Non-Emergency Medical Transport (a common DARP customer) |
| DSR | Drive/Scheduled Rest — driver-hours compliance |
| UAT | User Acceptance Testing |
| SLA | Service Level Agreement |
| FOSS | Free and Open-Source Software |

---

## Reference: the three research reports

If you want context on why we picked the fork path, read in this order:

1. **`ref/reports/dispatch-stability-research.md`** — the "chaotic shuffle" complaint and industry-standard solutions (skills pinning, ConVRP, Via/Spare UI patterns).
2. **`ref/reports/vroom-workarounds-inventory.md`** — our 33 workarounds and 1800 LoC of hacks around VROOM.
3. **`ref/reports/vroom-alternatives-research.md`** — why fork > Timefold > OR-Tools > others for our specific constraints.

Each is 2–5k words and self-contained.
