# Dispatch Re-Optimization Stability — Research Report

**Date**: 2026-04-18
**Context**: backend-dispatch (VROOM + Valhalla), passenger transfer to/from bus station
**Problem**: confirmed-passenger reshuffling between vehicles undermines dispatcher trust
**Author**: Research synthesis (no code changes)

---

## TL;DR

- **VROOM has no first-class "pin shipment to vehicle" primitive.** The closest mechanism is `vehicle.steps[]` in *solving mode*, but it has known sharp edges (issues #757, #800, #886) — VROOM uses your steps only as a *starting solution* for a single search path, not as a hard lock. The proven, robust workaround is **per-confirmed-task `skills`** (give each confirmed shipment a unique skill ID and grant only the assigned vehicle that skill). This is how the field actually pins in production with VROOM.
- **The academic field calls this the "Consistent VRP" (ConVRP)**: same driver/vehicle, same time, same territory across re-plans. Most production systems implement *driver consistency* via either (a) hard locking (skills/template routes), (b) penalty terms in the objective, or (c) two-tier solves (frozen-locked + free-pending).
- **Industry practice converges on a hybrid**: continuous re-optimization for *un-confirmed* requests + hard-lock or "pause optimization" for confirmed/in-progress trips. Spare ("Lock to Duty"), Via ("Auto Reassign" with explicit dispatcher approval), RideCo ("Solver" with manifest commitments), and Onfleet all expose this as an explicit dispatcher control rather than a silent global re-optimizer.
- **For your scale (10–50 requests/day/stop)**, a **two-tier solve** is the right answer: (1) confirmed shipments → `skills`-pin to assigned vehicle + tight time window (already done); (2) pending shipments → free VROOM solve over remaining vehicle capacity. Optimality loss is small because the dispatcher's confirmation order is approximately greedy-insertion order anyway.
- **Add a "proposed reassignment" UI affordance** for the rare case where a swap would dramatically improve the day (e.g., a late-cancelling passenger frees a seat). Show the dispatcher a diff and let them accept — never silent.

---

## 1. Academic Literature

### 1.1 The Consistent VRP (ConVRP) — Groer, Golden & Wasil (2009)

The seminal formulation. Defines a multi-day VRP where each customer must be served by **the same driver** (driver consistency) and at **approximately the same time** (time consistency, bounded by L hours of difference between earliest and latest arrival). Their solution approach builds *template routes* across the planning horizon and then perturbs them per-day. Subsequent template-based methods (Kovacs/Parragh/Hartl 2014's adaptive large neighborhood search) are direct descendants.

**Trade-off**: enforcing strict driver consistency typically costs 5–15% more vehicle-distance vs. an unconstrained solve, but customer/driver satisfaction metrics improve markedly (this is the standard finding across follow-up papers).

> Groer, Golden, Wasil. "The Consistent Vehicle Routing Problem." *Manufacturing & Service Operations Management* 11(4):630–643. 2009.
> https://pubsonline.informs.org/doi/10.1287/msom.1080.0243

### 1.2 Kovacs, Parragh, Hartl — survey (2014)

The canonical survey. Categorizes consistency into **service-time consistency**, **driver consistency**, and **territory/region consistency**. Reviews ~30 algorithmic approaches. Key takeaway for our problem: every production-grade approach to driver consistency reduces to one of three primitives:

1. **Hard pinning** via assignment constraints (template routes, skills, fixed assignment matrices)
2. **Soft pinning** via objective-function penalties on assignment changes
3. **Two-stage** decomposition (lock confirmed → optimize residual)

> Kovacs, Parragh, Hartl. "Vehicle routing problems in which consistency considerations are important: A survey." *Networks* 64(3):192–213. 2014.
> https://onlinelibrary.wiley.com/doi/10.1002/net.21565

### 1.3 The Generalized ConVRP — Kovacs, Golden, Hartl, Parragh (2015)

Generalizes ConVRP to handle *partial* driver consistency (e.g., "at least 80% of visits by the same driver") and arrival-time-window consistency. Critically introduces a **weighted bi-objective** formulation: total cost vs. consistency violation. Demonstrates that 2–4% optimality loss buys ~95% consistency in realistic instances. Highly relevant: this is the math-programming basis for "soft penalty" approaches.

> Kovacs, Golden, Hartl, Parragh. "The Generalized Consistent Vehicle Routing Problem." *Transportation Science* 49(4):796–816. 2015.
> https://pubsonline.informs.org/doi/10.1287/trsc.2014.0529

### 1.4 Dynamic Dial-A-Ride — rolling horizon & insertion heuristics

For **online** DARP (which is exactly your dispatcher's situation — requests arrive throughout the day), the dominant practical approach is **insertion heuristics**: keep existing routes, insert each new request at its lowest-cost feasible position. This is by construction "stability-preserving" because confirmed assignments never move.

- **Cheapest insertion**: try every (vehicle, position) pair, take the minimum-delta-cost one. O(n·m) per request. Existing assignments are immutable.
- **Regret insertion** (Diana & Dessouky, 2004): when inserting in batch, calculate for each pending request the *regret* = (cost of best position) − (cost of 2nd-best position). Insert highest-regret request first. Dramatically better quality than naive cheapest-first while still preserving stability.
- **Multiple plan approach** (Bent & Van Hentenryck, generalized in the OR Spectrum 2025 survey): keep a *pool* of K alternative plans; when a new request arrives, accept it into whichever plan accommodates it best. Hedges against future requests without committing to one plan.

> Diana, Dessouky. "A new regret insertion heuristic for solving large-scale dial-a-ride problems with time windows." *Transportation Research Part B* 38:539–557. 2004.
> https://bpb-us-w1.wpmucdn.com/sites.usc.edu/dist/0/249/files/2017/02/22A-New-Regret-Insertion-Heuristic-for-Solving-Large-scale-Dial-a-ride-Problems-with-Time-Windows22-Transportation-Research-Part-B-Methodological-38-539-557-2004-M.-Diana-and-M.-M.-Dessouky-PDF-28havuo.pdf

> Berbeglia, Cordeau, Laporte. "Dynamic pickup and delivery problems." *European Journal of Operational Research* (foundational review).

> "Multiple plan approach for a dynamic dial-a-ride problem." *OR Spectrum* (2025).
> https://link.springer.com/article/10.1007/s00291-025-00809-y

### 1.5 Disruption management & deviation cost

When a published plan must be revised, papers in this stream add an explicit **deviation cost** term to the objective: cost = travel + λ · (number_of_changed_assignments). Tuning λ controls the stability/quality trade-off directly. Recent results (Zhang et al. 2023, *Expert Systems with Applications*) report that "global adjustments achieve a superior balance between solution quality and stability, reducing request response time by over 16%, decreasing total travel distance by approximately 14%, and lowering service start-time deviations by about 62%" when stability terms are added. The lesson: **silent global re-optimization (your current behavior) is the worst of both worlds**; explicit stability cost is nearly free in the objective.

> Eksioglu, Vural, Reisman. "The vehicle routing problem: A taxonomic review." (deviation-cost framework discussed in the disruption-management chapter).

> "Disruption management in vehicle routing and scheduling for road freight transport: a review." *TOP* 26:1–50. 2018.
> https://link.springer.com/article/10.1007/s11750-018-0469-4

### 1.6 Rolling-horizon DARP

> "Solving the Dynamic Dial-a-Ride Problem Using a Rolling-Horizon Event-Based Graph." *OASIcs* (ATMOS 2021).
> https://drops.dagstuhl.de/entities/document/10.4230/OASIcs.ATMOS.2021.8

The rolling-horizon paradigm freezes commitments inside a "decision horizon" (typically the next H minutes) and re-optimizes only beyond it. Maps cleanly to a dispatcher workflow: pickups in the next 30 minutes are *committed* and untouchable; later pickups are still re-optimizable.

---

## 2. Industry Practice

### 2.1 Spare (spare.com)

Spare's product is the closest analog to your use case (paratransit/microtransit dispatch). They explicitly distinguish:

- **"Lock to Duty"** — dispatcher pins a trip to a specific vehicle ("ensuring passengers share a ride for accessibility or efficiency"). Once locked, the optimizer cannot move the trip.
- **"Pause Optimization"** — dispatcher freezes the entire manifest temporarily; useful during disruption/crisis when they need full manual control.
- **"Unmatched Requests"** — explicitly removes a trip from a manifest and re-queues for re-assignment.
- Their **Spare Engine** continuously re-optimizes the *un-locked* portion of the manifest at ~1-minute cadence using live traffic.

The pattern: **dispatcher trust is preserved by giving them a hard-lock primitive** that the optimizer never touches, not by trying to make the optimizer "smarter" about preserving stability.

> https://spare.com/blog/dispatchers-unsung-heroes-paratransit-microtransit-operations
> https://spare.com/blog/real-time-optimization-replacing-batch-scheduling-transit
> https://spare.com/products/spare-engine

### 2.2 Via Transportation (ridewithvia.com)

Via runs the largest commercial microtransit / paratransit fleet. Two distinct optimization modes:

- **Auto Assigner** — automatically places *unassigned* (pending) rides onto driver shifts when ViaAlgo determines feasibility. Never moves already-assigned rides silently.
- **Auto Reassign / Assisted Reassign** — explicitly invoked by the dispatcher for *one driver's remaining trips at once*. The dispatcher clicks a button and reviews a *proposed* reassignment before accepting. Reassignment "weighs several factors along with avoiding lateness."

The key design choice: **reassignment is a dispatcher-triggered, dispatcher-reviewed operation**, not a silent background optimization. Stability is preserved by default; optimization is opt-in.

> https://ridewithvia.com/resources/via-algos-reassign-assistant-move-todays-trips-with-guidance-from-our-efficiency-maximizing-algorithm

### 2.3 RideCo

RideCo's "Solver" engine reoptimizes manifests around **guaranteed arrival times**. The arrival-time guarantee functions as a hard constraint that the re-optimizer cannot violate, which implicitly stabilizes the schedule (since most reassignments would push arrival times). Re-optimization triggers are bounded events: driver call-outs, late boardings, no-shows, vehicle breakdowns — not "every input change."

> https://rideco.com/paratransit/dispatching

### 2.4 Onfleet

Onfleet's Route Optimization is **batch-style** (you click "optimize"). It does not silently re-optimize on every event. Re-optimization is dispatcher-triggered. This is explicitly a stability decision, not a technical limitation.

> https://support.onfleet.com/hc/en-us/articles/360023910371-Route-Optimization-Setup

### 2.5 OptimoRoute / Routific

Both target *next-day-planned* delivery with simple driver routes. Routific advertises "fast re-optimization" but explicitly positions it as a *dispatcher action*, not a continuous background process. Neither has rich pinning primitives; both expose a "lock stop to vehicle" toggle in the UI.

> https://www.routific.com/blog/optimoroute-alternatives

### 2.6 NextBillion.ai

Their Route Optimization API claims "incremental reoptimization … insert new orders or service requests into ongoing routes with minimal changes to the itinerary." This is essentially cheapest-insertion exposed as an API. Useful pattern reference but not a direct VROOM substitute.

> https://docs.nextbillion.ai/optimization/route-optimization-api

### 2.7 MOIA / Padam Mobility / Liftango

Less public detail. MOIA's published research focuses on offline shift planning and matching algorithms; recent work (NYU Tandon 2024) explicitly highlights "transfer commitments" as opportunity costs that should appear in the dispatch objective. The thrust: **once committed, treat the commitment as a real cost** and only break it if the gain exceeds it.

> https://engineering.nyu.edu/news/breakthrough-study-proposes-enhanced-algorithm-ride-pooling-services
> https://www.moia.io/en/blog/ridepooling

### 2.8 Industry consensus pattern

Across vendors:

1. **Explicit per-trip lock primitive** (Spare, Via, OptimoRoute) — the dispatcher controls what can move.
2. **Re-optimization is event-bounded, not input-triggered** (RideCo, Onfleet) — re-solve on cancellations/breakdowns, not on every new request.
3. **Proposed-changes UI** (Via's Reassign Assistant) — surface global improvements as suggestions, never apply silently.
4. **Continuous optimization applies only to un-locked portion** (Spare Engine).

Your current setup violates points 1, 2, and 3 simultaneously. That is the root cause of the perceived "shuffling," not VROOM itself.

---

## 3. VROOM Capabilities for Pinning

VROOM's documentation and issue tracker reveal **no first-class "pin task to vehicle" primitive**. There are three mechanisms that *can* be used, in increasing order of robustness:

### 3.1 `vehicle.steps[]` in solving mode (NOT reliable as a hard lock)

You can pre-seed a vehicle's route. The relevant doc text:

> "Using `steps` for vehicles in default VRP solving mode is a way to force starting the search from a matching user-defined solution, if valid. … This means a single search path is followed, starting from the provided solution. **Resulting quality is thus obviously expected to be highly dependent on the user-defined starting point.**"

JSON shape:

```json
{
  "vehicles": [{
    "id": 3,
    "start": [lon, lat],
    "end": [lon, lat],
    "steps": [
      { "type": "pickup",   "id": 4001 },
      { "type": "delivery", "id": 4001 },
      { "type": "pickup",   "id": 4002 },
      { "type": "delivery", "id": 4002 }
    ]
  }],
  "shipments": [
    { "id": 4001, "pickup": {...}, "delivery": {...} },
    { "id": 4002, "pickup": {...}, "delivery": {...} }
  ]
}
```

**Important caveat (issue #757, marked duplicate of #800):** users report VROOM's solver does not always honor `steps` as a lock — the solver can reorder, swap, or even drop a step if it improves the global objective. `steps` was designed as a *starting solution hint*, not a constraint. The feature request to add `"fixed": true` per step (issue #800) is **still open** as of 2025.

> https://github.com/VROOM-Project/vroom/issues/757
> https://github.com/VROOM-Project/vroom/issues/800
> https://github.com/VROOM-Project/vroom/issues/886
> https://github.com/VROOM-Project/vroom/blob/master/docs/API.md

### 3.2 Plan mode (`-c` / `choose_eta`) — for ETA recomputation only

If you give VROOM a fully pre-defined route and run with `-c`, it will *only* compute ETAs and report constraint violations. It will **not** re-optimize. Useful for "given this fixed assignment, what are the times?" but not for "optimize new requests around the locked ones" — because in plan mode, *nothing* gets optimized.

> https://github.com/VROOM-Project/vroom/issues/430

### 3.3 `skills` — the proven pinning workaround

This is what production VROOM users actually do. Skills are integers; VROOM enforces `job.skills ⊆ vehicle.skills`. By giving each confirmed shipment a **unique skill ID** and granting that skill to **only one vehicle**, you create a hard constraint that the optimizer cannot violate.

```json
{
  "vehicles": [
    { "id": 1, "skills": [1, 1001, 1002, 1005] },
    { "id": 2, "skills": [1, 1003, 1004] },
    { "id": 3, "skills": [1, 1006] }
  ],
  "shipments": [
    {
      "id": 1001,
      "skills": [1001],
      "priority": 100,
      "pickup":   { "location": [...], "time_windows": [[t-300, t+300]] },
      "delivery": { "location": [station_lon, station_lat] }
    },
    {
      "id": 9999,
      "skills": [1],
      "pickup":   { "location": [...], "time_windows": [[t1, t2]] },
      "delivery": { "location": [station_lon, station_lat] }
    }
  ]
}
```

In this example:
- Skill `1` = "any vehicle can serve" (the default for *pending* requests).
- Skills `1001..1006` = "this specific confirmed shipment can only go to the assigned vehicle."
- Vehicle 1 has skills `[1, 1001, 1002, 1005]` → can serve any pending shipment + confirmed shipments 1001/1002/1005.
- Confirmed shipment 1001 has skill `[1001]` → only Vehicle 1 has that skill, so VROOM **cannot reassign**.

This is robust, well-documented, and predictable. Combined with your existing `priority: 100` and tight `time_windows`, it gives full pinning (vehicle + time + position-via-time-window).

> https://github.com/VROOM-Project/vroom/issues/90 (skills design discussion)

### 3.4 What VROOM cannot do natively

- No soft penalty per swap (no `reassignment_cost` knob).
- No "warm start with this solution and don't deviate by more than K changes" mode.
- No native two-tier solve. You have to construct the two solves yourself by partitioning shipments and vehicles.

---

## 4. Practical Heuristics — Comparison Table

| Approach | Build cost | Optimality loss vs. unconstrained | Stability | Notes |
|---|---|---|---|---|
| **Skills-based pinning of confirmed shipments** (recommended) | Low — JSON-only change in your VROOM input builder | Small (~2–5%) for typical 10–50 req/day | High — confirmed never moves | Uses documented VROOM primitive. Combine with existing `priority`+`time_windows`. |
| **Two-tier solve: lock + free** | Medium — partition shipments/vehicles, two API calls, merge result | Slightly higher (~5–10%) than skills pinning because vehicle capacity is artificially split | Highest — confirmed shipments are not even *seen* by the second solve | Cleanest semantically. Slightly more code. Better when confirmed shipments are dense. |
| **Cheapest/regret insertion (no full re-solve)** | Medium — implement insertion logic outside VROOM, use VROOM only to score insertion deltas | Moderate (~10–15% over time as suboptimal insertions accumulate) | Maximal — existing routes literally never change | Best fit for true online dispatch. Periodic *manual* re-solve catches drift. |
| **Penalty-based stability (deviation cost)** | High — VROOM has no native support; would need to fork or post-process | Tunable (small at high λ, large at low λ) | Tunable | Not worth it — VROOM doesn't expose this knob. |
| **`vehicle.steps[]` initial-solution hint** | Low | Low (best case) — but unpredictable: solver may still move things | LOW — issue #757 confirms this is not a reliable lock | Not recommended as the primary mechanism. |
| **Plan mode (`-c`)** | Low | N/A — no optimization happens at all | Maximal but trivially | Use only as a verification/ETA-refresh step, never for re-optimization. |
| **Manual approval workflow ("propose changes")** | Medium-High — UI work, diff visualization, one-click accept/reject | Zero — dispatcher decides | Maximal by default | Pairs well with any of the above. Recommended as a *secondary* affordance, not primary. |
| **Rolling-horizon freeze (next H minutes immutable)** | Medium — partition by departure time | Low | High in the near term, full re-optimization beyond | Common in academic DARP, less common in dispatch UIs. Worth considering if you have far-future requests. |

---

## 5. Recommendation

### Primary: skills-based pinning + two-tier mental model

Your current setup (`priority: 100` + tight `time_windows: [t-300, t+300]`) is *almost* right. It just lacks vehicle pinning. Add a third constraint:

1. **Confirmed shipments**: assign a unique skill ID per confirmed shipment (e.g., `100000 + shipment_id`). Add that skill to *only* the assigned vehicle.
2. **Pending shipments**: leave skills empty (or assign a shared skill `[1]` that all vehicles have).
3. **Keep**: `priority: 100`, `time_windows` ±5 min on confirmed pickups.

Concrete VROOM input fragment:

```json
{
  "vehicles": [
    { "id": 1, "start": [...], "end": [...], "skills": [100123, 100456] },
    { "id": 2, "start": [...], "end": [...], "skills": [100789] },
    { "id": 3, "start": [...], "end": [...], "skills": [] }
  ],
  "shipments": [
    {
      "id": 123,
      "skills": [100123],
      "priority": 100,
      "pickup":   { "id": 1230, "location": [lon, lat], "time_windows": [[t-300, t+300]] },
      "delivery": { "id": 1231, "location": [station_lon, station_lat] }
    },
    {
      "id": 456,
      "skills": [100456],
      "priority": 100,
      "pickup":   { "id": 4560, "location": [lon, lat], "time_windows": [[t-300, t+300]] },
      "delivery": { "id": 4561, "location": [station_lon, station_lat] }
    },
    {
      "id": 789,
      "skills": [100789],
      "priority": 100,
      "pickup":   { "id": 7890, "location": [lon, lat], "time_windows": [[t-300, t+300]] },
      "delivery": { "id": 7891, "location": [station_lon, station_lat] }
    },
    {
      "id": 999,
      "pickup":   { "id": 9990, "location": [lon, lat], "time_windows": [[t1, t2]] },
      "delivery": { "id": 9991, "location": [station_lon, station_lat] }
    }
  ]
}
```

Result: shipment 123 *must* go to vehicle 1, 456 *must* go to vehicle 1, 789 *must* go to vehicle 2 — VROOM cannot violate the skill constraint. Pending shipment 999 is freely assignable.

**Implementation cost**: low. It is a JSON-construction change in the dispatch service's VROOM input builder. No algorithmic change.

**Optimality cost**: low at your scale (10–50 req/day). The dispatcher's confirmation order roughly approximates a greedy insertion order anyway, so the locked solution is rarely far from globally optimal.

### Secondary: a "propose reassignment" affordance

For the rare case where a swap would yield a clear improvement (e.g., a passenger cancels and now a confirmed assignment is wildly off), expose an explicit dispatcher action:

> **Re-balance day** (button in the dispatcher UI)

Behavior:
1. Run a *second* VROOM solve **without** the per-shipment skills (i.e., free re-optimization) but with `priority: 100` retained.
2. Diff result against current assignments.
3. Show the dispatcher the proposed swaps as a list ("Move Anna from Vehicle 3 to Vehicle 5, saves 12 min driving"). Each row has an Accept/Reject button.
4. Only apply the accepted swaps.

This mirrors Via's "Assisted Reassign" pattern. It gives the dispatcher access to the global optimum *when they want it*, without surprising them.

**Implementation cost**: medium (UI + diff logic). Worth doing once stability complaints stop.

### Why not pure cheapest-insertion?

It's tempting to skip VROOM entirely for confirmed-stable runs and just call VROOM for the new request alone. Don't — you lose the consolidation benefits across multiple pending shipments arriving in the same minute. The skills approach gives you the same stability guarantee while still letting VROOM optimize across all pending requests in a single solve.

### Things to verify before shipping

1. Confirm VROOM version supports `skills` on shipments (it does in 1.x and later — has since 2018).
2. Verify your skill IDs don't collide with any existing skill semantics (e.g., wheelchair-accessible vehicles). Use a high integer prefix (e.g., `1_000_000 + shipment_id`) to namespace.
3. Test the unfeasibility case: if a confirmed shipment's skill is on a vehicle that lacks capacity, VROOM will return the shipment as `unassigned` rather than violating the constraint. The dispatch UI should surface this clearly ("Vehicle 1 cannot accommodate Anna's pickup at the confirmed time — please re-confirm or unassign").
4. Consider a "release lock" admin action — if a vehicle breaks down, the dispatcher needs a one-click way to strip skills from that vehicle's confirmed shipments so they re-optimize onto other vehicles.

---

## References

### Academic
- Groer, Golden, Wasil. "The Consistent Vehicle Routing Problem." *M&SOM* 11(4):630–643, 2009. https://pubsonline.informs.org/doi/10.1287/msom.1080.0243
- Kovacs, Parragh, Hartl. "Vehicle routing problems in which consistency considerations are important: A survey." *Networks* 64(3), 2014. https://onlinelibrary.wiley.com/doi/10.1002/net.21565
- Kovacs, Golden, Hartl, Parragh. "The Generalized Consistent Vehicle Routing Problem." *Transportation Science* 49(4), 2015. https://pubsonline.informs.org/doi/10.1287/trsc.2014.0529
- Diana, Dessouky. "A new regret insertion heuristic for solving large-scale dial-a-ride problems with time windows." *TR-B* 38, 2004. https://bpb-us-w1.wpmucdn.com/sites.usc.edu/dist/0/249/files/2017/02/22A-New-Regret-Insertion-Heuristic-for-Solving-Large-scale-Dial-a-ride-Problems-with-Time-Windows22-Transportation-Research-Part-B-Methodological-38-539-557-2004-M.-Diana-and-M.-M.-Dessouky-PDF-28havuo.pdf
- "Solving the Dynamic Dial-a-Ride Problem Using a Rolling-Horizon Event-Based Graph." *OASIcs* (ATMOS 2021). https://drops.dagstuhl.de/entities/document/10.4230/OASIcs.ATMOS.2021.8
- "Multiple plan approach for a dynamic dial-a-ride problem." *OR Spectrum*, 2025. https://link.springer.com/article/10.1007/s00291-025-00809-y
- "Disruption management in vehicle routing and scheduling for road freight transport: a review." *TOP* 26, 2018. https://link.springer.com/article/10.1007/s11750-018-0469-4
- "The consistent vehicle routing problem considering driver equity and flexible route consistency." *Computers & Operations Research*, 2024. https://www.sciencedirect.com/science/article/abs/pii/S0360835223008276
- Insertion Heuristics survey: https://optimization-online.org/wp-content/uploads/2022/11/dynamic-insertion.pdf

### Industry
- Spare — Dispatchers blog: https://spare.com/blog/dispatchers-unsung-heroes-paratransit-microtransit-operations
- Spare Engine product page: https://spare.com/products/spare-engine
- Spare — real-time vs batch: https://spare.com/blog/real-time-optimization-replacing-batch-scheduling-transit
- Via — Reassign Assistant: https://ridewithvia.com/resources/via-algos-reassign-assistant-move-todays-trips-with-guidance-from-our-efficiency-maximizing-algorithm
- Via — Paratransit: https://ridewithvia.com/solutions/paratransit
- RideCo Dispatching: https://rideco.com/paratransit/dispatching
- Onfleet Route Optimization: https://support.onfleet.com/hc/en-us/articles/360023910371-Route-Optimization-Setup
- Routific competitor analysis: https://www.routific.com/blog/optimoroute-alternatives
- NextBillion.ai Route Optimization: https://docs.nextbillion.ai/optimization/route-optimization-api
- MOIA Ridepooling research: https://solutions.moia.io/en/research
- NYU Tandon ride-pooling: https://engineering.nyu.edu/news/breakthrough-study-proposes-enhanced-algorithm-ride-pooling-services

### VROOM
- VROOM API docs: https://github.com/VROOM-Project/vroom/blob/master/docs/API.md
- Issue #90 — Skills design: https://github.com/VROOM-Project/vroom/issues/90
- Issue #430 — choose_eta plan mode: https://github.com/VROOM-Project/vroom/issues/430
- Issue #576 — Enforce shipments to be made by the same vehicle: https://github.com/VROOM-Project/vroom/issues/576
- Issue #643 — VehicleStep semantics: https://github.com/VROOM-Project/vroom/issues/643
- Issue #704 — More steps in shipments: https://github.com/VROOM-Project/vroom/issues/704
- Issue #757 — vehicle_step not respected: https://github.com/VROOM-Project/vroom/issues/757
- Issue #800 — Allow defining immutable tasks (open feature request): https://github.com/VROOM-Project/vroom/issues/800
- Issue #886 — Forcing vehicle steps in solving mode: https://github.com/VROOM-Project/vroom/issues/886
