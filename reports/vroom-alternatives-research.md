# VROOM Alternatives — Survey for Dispatch DARP

**Date**: 2026-04-18
**Context**: `backend-dispatch` (VROOM + Valhalla), passenger transfer to/from bus station
**Workload**: ~10–50 daily shipments per stop, sub-second solve required for interactive dispatcher UI
**Purpose**: identify a possible replacement or supplement for VROOM, given the gaps surfaced in the prior dispatch-stability research (no hard pin primitive, no soft re-assignment penalty, limited diagnostics, no incremental re-solve)
**Author**: Research synthesis (no code changes)

---

## TL;DR

- **Stay on VROOM for the solver, layer two-tier orchestration in `backend-dispatch`.** No alternative beats VROOM on the combination of (a) self-hostable BSD-2 license, (b) sub-500 ms solve at our scale, (c) trivial Valhalla integration, (d) plain JSON HTTP API. The stability gaps are real but cheaper to close in our orchestration layer than to migrate.
- **Best FOSS upgrade if we ever outgrow VROOM: Timefold Solver.** It is the only open-source candidate that natively offers (1) `@PlanningPin` / `@PlanningPinToIndex` for hard-pinning, (2) `publishedValue` shadow variables for *soft* deviation penalties, and (3) explicit `ProblemChange` warm-restart — exactly the three primitives we are missing. Cost: a JVM service in our PHP/Vue stack and a serious modelling exercise (constraint streams in Java/Kotlin). Plan for ~2–4 weeks to reach feature parity.
- **Best commercial fallback if we want to stop maintaining a solver: Hexaly** for raw speed and lower bounds, or **Solvice OnRoute** (Belgian, EU-hosted, ~€16/resource/month, REST-first) if we want a managed API that already speaks our problem shape. Both are cloud-only — Hexaly is rentable as a worker license for self-host; Solvice is SaaS only. Avoid Google/Mapbox/HERE Tour Planning for our shape: their billing is per-shipment-per-call and their re-optimization story for sub-second interactive UI is weaker than VROOM's.
- **OR-Tools CP-SAT routing** is the obvious "industry-standard" alternative but is *not* a clear win for us: it has true `ApplyLocksToAllVehicles` for hard locks (good), but solve time on DARP-with-time-windows at our scale is typically 2–10× slower than VROOM, and the C++/Python bindings would need a new sidecar service. Worth keeping on the radar if we ever need consistency-VRP / multi-day template routes.
- **PyVRP and JSPRIT** are interesting research engines but neither has first-class DARP semantics (max ride time, paired pickup/delivery time windows) plus a stable HTTP server, so adopting either means significant glue code.
- **Microtransit-platform engines (Spare, Via, RideCo, Liftango)** are not standalone-licensable. They sell the whole stack. Useful only as a "buy not build" exit strategy if the dispatcher product itself stops being a differentiator.

---

## Comparison Matrix

| Tool | License | Self-host | Speed at our scale (50 shipments) | Hard pin | Soft penalty | Re-opt API | DARP fit | Maturity (last release) |
|------|---------|-----------|-----------------------------------|----------|--------------|------------|----------|--------------------------|
| **VROOM** (incumbent) | BSD-2 | Yes (Docker) | 100–500 ms | via `skills` workaround (issue #757, #886) | No | No (full re-solve) | Native (shipments + TWs + capacity) | Active; v1.15 (Mar 2024), v1.14 (Jan 2024) |
| **OR-Tools** routing | Apache 2.0 | Yes (lib) | 0.5–5 s typical | Yes (`ApplyLocksToAllVehicles`, `close_routes`) | Via custom dimensions | Warm-start via `ReadAssignment` | Native (PD pairs, max ride time via dimensions) | Active; v9.x quarterly |
| **Timefold Solver** | Apache 2.0 | Yes (JVM) | Comparable to VROOM with tuning | Yes (`@PlanningPin*`) | Yes (`publishedValue` constraint) | Yes (`ProblemChange`, partial pinning) | Modelled, not built-in (~few hundred LoC) | Very active; v1.30 (2026) |
| **PyVRP** | MIT | Yes (lib) | 200–800 ms (HGS) | Limited (research) | No | No (full re-solve) | Pickup/delivery yes, full DARP partial (issue #441) | Active; rapid releases |
| **JSPRIT** | Apache 2.0 | Yes (JVM) | 1–10 s | Limited (initial routes) | Limited | No | PD with TWs | Maintenance-mode; 1.9.0-beta.18 (Sep 2025) |
| **VRPy** | MIT | Yes (lib) | 5–30 s (column gen) | N/A | N/A | No | CVRPTW yes, no DARP | Slow (academic); 2020 paper |
| **Hexaly** (ex-LocalSolver) | Commercial; free academic | Yes (license) + Cloud | sub-second on 50–100; 2.3% gap on 1000 PDPTW in 1 min | Yes (model constraints) | Yes (objective terms) | Yes (model is editable) | Excellent (PDPTW first-class) | Very active; v14.5 (2026) |
| **Google Route Optimization API** | Commercial | No (cloud) | Variable; not optimised for sub-s on small | Yes (shipment.pickups[].label) | Cost-based | No | Yes | GA on Maps Platform |
| **Mapbox Optimization v2** | Commercial; **beta** | No | Unclear | Limited | Limited | No | Yes (PD + TWs in beta) | Beta as of 2025 |
| **HERE Tour Planning v3.1** | Commercial | No (cloud) | Multi-second async | Limited | Limited | No | Yes | Active |
| **GraphHopper Route Optimization** | Commercial; ext. of FOSS engine | Yes (Enterprise) | Sub-second on tens of stops | Yes (initial routes) | Limited | No | Yes | Active |
| **NextBillion.ai** | Commercial | No (cloud) | Unclear; multi-region | Yes (`vehicle.fixed_jobs`) | Limited | Yes (re-optimize endpoint) | Yes | Active |
| **Routific API** | Commercial | No | Sub-second on 100 stops | No (deliveries only) | No | No | No DARP | Active |
| **Solvice OnRoute** | Commercial | No (EU SaaS) | Sub-second on tens | Yes (frozen tasks) | Yes (objective weights) | Yes (re-solve endpoint) | Yes | Active; "OnRoute" rebrand 2025 |

---

## 1. Open-Source Candidates

### 1.1 Google OR-Tools (CP-SAT routing)

**What it is**: Google's mature C++ optimization library with first-class routing module (`pywrapcp`). The CP-SAT solver inside is gold-medal-grade on MiniZinc challenges; the routing layer wraps it with VRP-specific heuristics (PATH_CHEAPEST_ARC initial solution + Guided Local Search metaheuristic).

**Strengths vs VROOM**:
- **True hard locks**: `ApplyLocksToAllVehicles(locks, close_routes=true)` deactivates remaining nodes and pins specified sub-routes — this is exactly the primitive we hack with `skills`.
- **Pickup-and-delivery is first-class**: `AddPickupAndDelivery` plus dimension precedence enforces same-vehicle and pickup-before-delivery natively.
- **Warm-start via `ReadAssignment`** lets us seed the search with the previous solution → in practice this yields stability-preserving re-solves without an explicit "diff" API.
- **Custom dimensions** are fully exposed: we can model max passenger ride time, luggage capacity, vehicle-task affinity penalties as soft costs.

**Weaknesses vs VROOM**:
- **Slower at our scale**: independent reports put OR-Tools at 0.5–5 s for 50–100 stop instances vs VROOM's 100–500 ms. CP-SAT is built to prove optimality / handle large complex problems; VROOM optimises hard for "fast good-enough on small VRP".
- **No HTTP server out of the box** — we'd ship a Python or Java sidecar, define a JSON contract, and re-implement what `vroom-express` gives us free.
- **Steep learning curve**: building a working DARP-with-stability model in OR-Tools is several hundred lines and requires deep familiarity with dimensions, callbacks, and search strategies.
- **No native diagnostics**: same problem as VROOM — when the solver returns a weird solution, you're inspecting bare assignments.

**Closes which gap**: hard-pin (#1), partial soft-penalty via dimensions (#2), warm-start re-opt (#4). Does *not* close diagnostics (#3) or multi-objective UI (#6).

**Links**:
- https://developers.google.com/optimization/routing/pickup_delivery
- https://developers.google.com/optimization/routing/routing_options
- https://github.com/google/or-tools/discussions/2850
- https://acrogenesis.com/or-tools/documentation/user_manual/manual/vrp/partial_routes.html

### 1.2 Timefold Solver

**What it is**: Active fork of OptaPlanner by the original OptaPlanner team (founded 2023). Constraint-based meta-heuristic engine in Java/Kotlin with Quarkus and Spring Boot starters. Specifically markets itself as a *real-time replanning* engine — pinning, problem-change events, partial list pinning, published-value comparison are first-class concepts.

**Strengths vs VROOM** (this is the candidate that closes the most gaps):
- **`@PlanningPin` and `@PlanningPinToIndex`** — entity-level and partial-list-level hard pinning, exactly the mental model we need for "passengers already in flight cannot move".
- **`publishedValue` / nonvolatile replanning** — explicit constraint term `if (currentVehicle != publishedVehicle) penalty(1000)` is the ConVRP-style soft penalty we are missing in VROOM.
- **`ProblemChange` API** — modify the working solution incrementally (add request, cancel request, change vehicle availability) without re-building from scratch. The solver does a warm restart, only newly-uninitialized variables run construction heuristics.
- **Score explanation** — Timefold can explain *why* a solution scores what it does, term-by-term. This addresses our "limited diagnostics" complaint directly.
- **Active development**: v1.30 in 2026, ~2× faster than legacy OptaPlanner, comprehensive VRP examples shipped.

**Weaknesses vs VROOM**:
- **JVM dependency**: adds a new runtime to a stack that is otherwise PHP+TS. Operationally non-trivial.
- **No DARP-out-of-the-box**: the shipped vehicle-routing quickstart is delivery-only. We'd need to model paired pickup/delivery, max ride time, and shared capacity ourselves (a few hundred lines of constraint streams — well-trodden but real work).
- **Tuning required**: Timefold needs config for construction heuristic + local search phases to match VROOM speed at our scale. Rule of thumb from Timefold itself: parity needs a few benchmark iterations.
- **Less proven for sub-second interactive solves**: Timefold's marketing emphasizes "real-time" but the documented scores-per-second numbers (1000+/s) imply we'd target 1–2 s, not 100 ms, for interactive UX. Acceptable but a regression.

**Closes which gap**: hard pin (#1), soft penalty (#2), diagnostics (#3 — best in class), incremental re-opt (#4), edit-distance (#5), multi-objective (#6, via weighted constraints).

**Links**:
- https://timefold.ai/blog/continuous-planning-optimization-with-pinning
- https://docs.timefold.ai/timefold-solver/latest/responding-to-change/responding-to-change
- https://docs.timefold.ai/field-service-routing/latest/real-time-planning/real-time-planning-pinning-visits
- https://github.com/TimefoldAI/timefold-solver

### 1.3 JSPRIT

**What it is**: Java VRP toolkit, originally developed inside GraphHopper. Solves CVRP, VRPTW, PDPTW with multiple depots, skills, etc., using ruin-and-recreate metaheuristics.

**Strengths**: BSD-style license, Java library suits JVM shops, decent constraint set, OK community.

**Weaknesses vs VROOM**:
- **In maintenance mode**: latest stable is 2.0.0; 2025 activity is intermittent (1.9.0-beta.18 published Sep 2025, scattered issue triage). Not abandoned but not investing.
- **Slower than VROOM** in published comparisons; GraphHopper themselves moved their commercial Route Optimization API to a proprietary engine, suggesting JSPRIT was outgrown.
- **No HTTP server** included — write your own.
- **No first-class hard pinning**: `InitialRoutes` exist but are search-only seeds, the same trap VROOM has.

**Verdict**: skip. If we're going JVM, Timefold is strictly better.

**Links**:
- https://github.com/graphhopper/jsprit
- https://jsprit.github.io/

### 1.4 PyVRP

**What it is**: Modern Python package (2023+) implementing the HGS (Hybrid Genetic Search) metaheuristic. Won the 2021 DIMACS VRPTW challenge and the 2022 EURO/NeurIPS dynamic VRP competition. Heavy C++ core under a clean Python API.

**Strengths**:
- **Best-in-class solution quality on standard VRPTW benchmarks** — the 2025 PyVRP+ research paper reports 2.7% quality gain and 45% runtime cut over the HGS baseline.
- **Easy to install** (pip), nice Python API, good docs.
- **Active development** with rapid release cadence.

**Weaknesses vs VROOM**:
- **DARP support is partial** — issue #441 lists "common VRP variants"; full pickup-delivery with max ride time and paired time windows isn't a native primitive yet. Would need extension.
- **Library only**, no HTTP server. We'd write one in Python.
- **No hard-pin primitive** at the API level; a research codebase, not a production scheduling kit.
- **Unproven at sub-second target on DARP shape** — the benchmarks they win run for seconds-to-minutes.

**Verdict**: most interesting if we want to *experiment* with a higher-quality solver to see if we're leaving optimality on the table. Not a production replacement today.

**Links**:
- https://pyvrp.org/
- https://pubsonline.informs.org/doi/10.1287/ijoc.2023.0055
- https://github.com/PyVRP/PyVRP/issues/441

### 1.5 VRPy

**What it is**: Python package using column generation (set-partitioning master + shortest-path-with-resource-constraints subproblem) for CVRP/CVRPTW. Academic provenance (JOSS paper 2020).

**Strengths**: Mathematically clean, exposes full LP relaxation, supports heterogeneous fleet.

**Weaknesses vs VROOM**:
- **Slow** — column generation is seconds-to-minutes, not milliseconds. Wrong tool for interactive UI.
- **No DARP support**, no pinning, no HTTP server, low recent activity.

**Verdict**: skip for production use; might be useful as an offline "what's the LP lower bound for this day's plan" auditor.

**Links**: https://github.com/Kuifje02/vrpy, https://joss.theoj.org/papers/10.21105/joss.02408

### 1.6 Hexaly Optimizer (ex-LocalSolver)

**What it is**: Commercial generic optimization solver with a strong VRP / routing modelling library. Rebranded from LocalSolver in 2024. Known for very fast convergence on real-world large instances.

**Strengths**:
- **Speed**: LocalSolver 12.0 (2025) reports 2.3% optimality gap on 1000-delivery CVRPTW and 1.7% on 1000-delivery PDPTW *within 1 minute*. At our scale (50 stops) it's well sub-second.
- **First-class PDPTW** modelling, native max-ride-time and capacity primitives, lower-bound computation (rare and useful for diagnostics).
- **Geodata module** ships its own matrix engine — could replace Valhalla for some scenarios.
- **Hexaly Cloud** offers managed worker mode; on-premise license is also available.
- **Free academic license** (3 months, renewable forever) — useful for prototyping.

**Weaknesses vs VROOM**:
- **Commercial license** with non-trivial price (Hexaly's published model is "annual flat per-seat", quoted on contact; rough industry hearsay puts it in the €10K–€50K/year range for a single-product commercial license — verify with vendor).
- **Modelling language** (LSP / Python API) is a different paradigm — not just "swap the HTTP endpoint". Migration is a real engineering effort.
- **Closed source** — we can't extend or audit.

**Verdict**: the only candidate that plausibly improves *both* speed and feature set vs VROOM. Worth a free-tier prototype if we ever feel solver-bound. Not justified by current pain.

**Links**:
- https://www.hexaly.com/
- https://www.hexaly.com/announcements/new-release-localsolver-11-5
- https://www.hexaly.com/pricing

---

## 2. Commercial APIs

### 2.1 Google Route Optimization API

**What it is**: Google's managed VRP/PDPTW solver, sold via Google Maps Platform. Replaced the older Cloud Fleet Routing in 2024.

**Pricing (post-March-2025 changes)**: per-shipment SKU, billed at every solve. Single-vehicle SKU and Fleet (≥2 vehicles) SKU. Free monthly cap replaced the old $200 credit. Specific per-shipment $ rate not published — pricing page links to a calculator and contract.

**Pros**: high-quality road network globally, good DARP feature coverage (shipments with TWs, capacity, breaks, skills, label-based pinning).

**Cons for us**:
- **Per-call billing** is hostile to interactive dispatcher UI: every dispatcher poke is a $-charge. At 50 shipments × dozens of solves/day across several dispatchers, runs to high three-digit dollars/month easily.
- **Cloud-only**; data leaves our infra.
- **Sub-second SLA not guaranteed** for small problems (the API is tuned for large fleet planning).
- **Ukraine map data**: Google has it, but we already have Valhalla on OSM tuned for our use case.

**Verdict**: skip — wrong economics for interactive solver use.

**Links**:
- https://developers.google.com/maps/documentation/route-optimization/usage-and-billing
- https://mapsplatform.google.com/pricing/

### 2.2 Mapbox Optimization API v2

**What it is**: Mapbox's next-gen VRP API. **Still in beta** as of 2025 — sign-up gated.

**Features**: time windows, vehicle capacity, driver shifts, pickup-and-drop-off constraints. Uses Mapbox's own road network.

**Cons for us**:
- **Beta** — no production SLA, feature set may shift.
- **Pricing not public** for v2.
- **Mapbox Ukraine coverage** is OSM-derived but their commercial routing tiles may not match our Valhalla tuning for back-roads pickup.
- **No pinning / re-opt story documented** for v2 yet.

**Verdict**: monitor; not actionable today.

**Links**: https://docs.mapbox.com/api/navigation/optimization/, https://www.mapbox.com/contact/optimization-v2-api

### 2.3 HERE Tour Planning API (v3.1)

**What it is**: HERE's enterprise tour planning service. Pickup-and-delivery jobs, multi-stop, capacity, breaks.

**Pros**: Enterprise pedigree, AWS Marketplace listing, rich documented use cases.

**Cons for us**:
- **Async/job-based API** — tour planning requests are submitted then polled. Not a sub-second interactive fit.
- **Pricing**: per-transaction (each location counts) — billed via HERE platform plans, no flat hobby tier.
- **Ukraine maps**: HERE has commercial coverage but again duplicates what Valhalla already gives us.
- **No documented re-optimization-with-pinning workflow** for our scale.

**Verdict**: skip. Built for fleet planning, not dispatcher UI.

**Links**: https://www.here.com/docs/category/tour-planning-api, https://developer.here.com/documentation/tour-planning/3.1/dev_guide/topics/use-cases/pickup-delivery.html

### 2.4 GraphHopper Route Optimization API

**What it is**: Commercial extension of the FOSS GraphHopper routing engine. A proprietary VRP solver wrapped in a managed API; GraphHopper also licenses an Enterprise on-prem version.

**Pricing**: $56–$384/month (Basic to Premium) on hosted, with credit-based usage; Enterprise quote-based.

**Pros**: established player, decent DARP coverage, REST API, supports `initial_routes` for pinning, can run on-prem under Enterprise license.

**Cons for us**:
- **30–200 locations per request, 2–20 vehicles per request** on the standard tiers — at the upper end of our volume but not generous for batch backfill.
- **On-prem pricing is opaque** — likely 5-figure annual.
- **Solver internals are closed** (the FOSS GraphHopper engine is the routing/matrix layer; the optimizer is proprietary).

**Verdict**: a serious commercial option *if* we want to outsource solver maintenance, lower-mid-tier price. Solvice (next) is broadly comparable for less money.

**Links**: https://www.graphhopper.com/route-optimization/, https://www.graphhopper.com/pricing/

### 2.5 NextBillion.ai Route Optimization API

**What it is**: Singapore/India-based commercial routing+optimization API.

**Pricing**: per-call / per-asset / per-task; custom contracts; AWS Marketplace listing.

**Pros**: explicit `vehicle.fixed_jobs` for pinning, dedicated re-optimize endpoint, claims real-time use cases.

**Cons for us**:
- **Cloud-only**, opaque pricing requires sales call.
- **Ukraine coverage** is presumably OSM-based but we've not seen public quality reports for our region.
- Marketing-heavy site, harder to evaluate without direct trial.

**Verdict**: viable commercial fallback if EU vendors don't fit; engage sales for an evaluation.

**Links**: https://docs.nextbillion.ai/optimization/route-optimization-api, https://nextbillion.ai/pricing

### 2.6 Routific API

**Pricing**: free <100 orders/month; flat $150/month up to 1000; per-order beyond.

**Verdict**: **skip** — Routific is a delivery-route optimizer (drop-off only). No paired pickup-delivery primitive matching our DARP shape.

**Links**: https://dev.routific.com/pricing

### 2.7 Solvice OnRoute

**What it is**: Belgian (EU-hosted) routing API explicitly positioned as a Google Routes API alternative. REST-first, "purpose-built routing AI solver".

**Pricing**: from €16/resource/month, minimum 10 resources. Volume discount above 500 resources. 30-day free trial.

**Pros**:
- **EU data residency** (relevant for GDPR / future Ukraine-EU data-flow rules).
- **Explicit re-optimize endpoint** with frozen tasks.
- **Reasonable economics** at our scale (€160/month minimum is in the noise).
- Active blog with technical depth on routing / optimization.

**Cons for us**:
- **Cloud only** — no self-hosted option mentioned.
- **Less brand recognition** than Hexaly / Google.
- **DARP-specific features** (paired pickup/delivery time windows + max ride time) need verification on a trial.

**Verdict**: the most attractive *commercial* alternative for our shape. If we ever decide to stop maintaining a solver, this is the first one to trial.

**Links**:
- https://www.solvice.io/route-optimization-api
- https://www.solvice.io/pricing
- https://www.solvice.io/post/google-routes-api-alternatives-route-optimization-apis-for-2025

### 2.8 Verso (commercial VROOM)

**What it is**: Verso is the commercial steward of the VROOM project — same engine, plus enhancements and SaaS hosting.

**Pricing**: per-optimized-task usage-based.

**Verdict**: this is "VROOM with a contract" — relevant only if we want commercial backing for the same engine we already run. Doesn't close any of the architectural gaps that motivated this report.

**Links**: https://verso-optim.com/, https://docs.verso-optim.com/api/vrp/v1/

---

## 3. Bundled Microtransit Platforms

These vendors sell the whole stack — solver, dispatcher app, driver app, rider app, BI. Their optimization engines are not (publicly) licensable standalone.

- **Spare** (Spare Engine + Spare Platform) — engine described in their marketing as continuously re-optimizing un-locked manifests; "Lock to Duty" is the dispatcher primitive we want. Engine is not standalone-sellable.
- **Via Transportation** — proprietary solver behind Via for Cities and Via TransitTech; not publicly licensable. They do sell software-only deployments to transit agencies but as a turnkey product, not a solver SDK.
- **RideCo** — "Solver" is their patented optimization engine; exposed via "RideCo Connect" *integration* APIs but not as a general-purpose VRP solver license.
- **Liftango** — APAC microtransit; partnered with Optibus in 2024. Standalone solver licensing not advertised.
- **Optibus** — primarily fixed-route scheduling (their "OnSchedule" product) plus Liftango integration for on-demand. Quote-based enterprise pricing; not a standalone VRP solver.

**Implication for us**: the bundled platforms are a *replacement for the entire dispatcher product*, not a solver swap. Useful as a benchmark for UX (we already do this — see the prior stability research) but not a procurement option for just the optimizer.

**Links**:
- https://spare.com/products/spare-engine
- https://www.rideco.com/differentiator/solver
- https://blog.optibus.com/announcements/optibus-and-liftango-partner-to-deliver-integrated-fixed-route-and-on-demand-transport-platform

---

## 4. Benchmarks & Comparisons

**No public head-to-head VROOM-vs-OR-Tools benchmark exists for our exact shape (small DARP with passenger ride-time constraints, sub-second target).** What we have:

- **VROOM official benchmarks** (https://github.com/VROOM-Project/vroom/wiki/Benchmarks): 1.63% gap on Solomon CVRPTW, ~360 ms average for 100-stop instances. Verified by Verso reports.
- **OR-Tools published benchmark** (https://research.google/pubs/or-tools-vehicle-routing-solver-...): the third-party blog comparison at https://www.singdata.com/trending/comparing-vrp-solvers-ortools-optaplanner-saas/ reports OR-Tools solving a 318-node instance in ~92 seconds with a 19% optimality gap — i.e. OR-Tools is *much* slower at finding good DARP solutions than VROOM is at solving smaller VRPTW. Not apples-to-apples but directionally tells us VROOM's speed lead is real.
- **Hexaly LocalSolver 12.0 self-published**: 2.3% gap on 1000-delivery CVRPTW within 1 minute, 1.7% on 1000-delivery PDPTW within 1 minute (https://www.hexaly.com/announcement/new-release-localsolver-12-0). On our scale (50 shipments) this is sub-second territory. Hearsay because vendor-published.
- **PyVRP DIMACS / EURO-NeurIPS wins** are rigorous third-party benchmarks but on CVRP / dynamic VRP, not DARP. Translates to "PyVRP is a state-of-the-art VRP engine"; doesn't translate directly to our problem.
- **Cordeau-Laporte DARP instances** (a-2-20 through a-4-32, plus larger pr / school / center instances) are the gold standard for DARP comparisons. No public scoreboard tracks 2024–2026 solver entries; the field is dominated by ALNS variants in academic papers (avg 0.49% gap from BKS reported in recent tabu-search/LNS work). VROOM is *not* benchmarked on Cordeau DARP instances publicly — its sweet spot is VRPTW/PDPTW.

**Practical takeaway**: at our problem scale (50 shipments × 5–10 vehicles × time-windows), every serious solver will give us "good enough" answers. Speed and integration cost dominate; absolute optimality does not. VROOM wins on both — which is why no obvious migration target jumps out.

---

## 5. Recommendation

### 5.1 If we want to fix the gaps WITHOUT switching (preferred — lowest cost, highest leverage)

Build the orchestration the prior stability report recommended, on top of unmodified VROOM:

1. **Two-tier solve in `backend-dispatch`**: confirmed shipments use `skills` to hard-pin (already done); pending shipments are solved freely against remaining capacity. This closes gap #1 (hard pin) operationally.
2. **Soft-penalty pre-pass in our service**: when re-solving, compute the previous assignment, then optionally seed VROOM with `vehicle.steps[]` (acknowledge it's a hint, not a constraint) *and* post-process: if VROOM moved a shipment off its previous vehicle, evaluate the cost delta — accept silently if delta > threshold, else discard the change. This approximates gap #2 (soft penalty) at the dispatcher level.
3. **Diagnostics layer**: log VROOM input/output diffs into our `/admin/dispatch/audit` view; surface `summary.unassigned_reasons` in the UI. Closes gap #3 with no upstream change.
4. **Stability-aware UI affordance**: "Re-optimize this manifest" button (manual, scoped) instead of silent global re-solve. Closes the trust problem regardless of solver capabilities.
5. **Watch upstream VROOM for a hard-pin primitive**: issues #757, #800, #886 are all open and active community-wise. The `vehicle_pinning` branch reference in #886 suggests work in progress. Subscribe.

**Effort**: ~2 weeks in `backend-dispatch`. **Risk**: low.

### 5.2 If we want to switch to FOSS

**Pick: Timefold Solver.** It's the only FOSS engine that natively offers all three primitives we want (pin / soft-penalty / incremental re-opt) plus a real diagnostics story.

**Migration cost estimate**:
- Stand up a JVM service (Quarkus or Spring Boot template, ~2 days).
- Re-model our DARP in `@PlanningEntity` / constraint-stream constraints (~1 week including paired PD, max ride time, capacity, soft deviation penalty).
- HTTP contract compatible with current `backend-dispatch` interface (~2 days).
- Tuning to reach VROOM-comparable solve time at our scale (~1 week — needs benchmarking).
- Operations: JVM monitoring, GC tuning, container image (~3 days).

**Total**: ~3–4 weeks engineering, plus ongoing JVM ops cost. Worth it only if (a) the orchestration approach above hits real ceilings, or (b) we expect significant DARP feature growth (multi-day templates, ConVRP, 5+ optimization objectives) where Timefold's flexibility pays off.

**Hexaly is the runner-up**: faster than Timefold, better lower bounds, but commercial license + closed source moves it into category 5.3.

### 5.3 If we want to switch to a commercial API

**Pick: Solvice OnRoute** for the right combination of EU hosting, REST-first ergonomics, explicit re-optimize endpoint, and predictable per-resource pricing.

**Cost estimate for our volume**:
- Assume 10 vehicles dispatched per stop, 5 stops in active use → 50 resources × €16/mo = **€800/mo (~$870/mo)** at the smallest tier. Volume discounts kick in above 500 resources, which we won't hit.
- Plus: we keep Valhalla (Solvice will use its own road network unless we negotiate a custom matrix endpoint — verify in trial).
- Plus: glue code in `backend-dispatch` to translate our problem to Solvice's schema (~1 week).

**Total**: ~€10K/year + 1–2 weeks integration. Comparable to the cost of operating VROOM/Valhalla containers but with a vendor on the hook for SLA. **Not justified given VROOM works for our scale**, but a realistic option if maintenance burden ever becomes a topic.

**If we'd rather pay for raw speed and self-host**: Hexaly with a worker license. 5-figure annual but we own the deployment, can run on our K8s, and get the best-in-class speed/quality numbers in the comparison matrix.

---

## References

- VROOM project: https://github.com/VROOM-Project/vroom
- VROOM benchmarks wiki: https://github.com/VROOM-Project/vroom/wiki/Benchmarks
- VROOM issue #886 (vehicle steps not respected as constraint): https://github.com/VROOM-Project/vroom/issues/886
- VROOM issue #757 (vehicle_step not respected): https://github.com/VROOM-Project/vroom/issues/757
- Verso (commercial VROOM): https://verso-optim.com/, https://docs.verso-optim.com/api/vrp/v1/
- OR-Tools routing pickup/delivery: https://developers.google.com/optimization/routing/pickup_delivery
- OR-Tools partial routes / locks: https://acrogenesis.com/or-tools/documentation/user_manual/manual/vrp/partial_routes.html
- OR-Tools "force vehicles to visit certain locations" discussion: https://github.com/google/or-tools/discussions/2850
- OR-Tools paper: https://research.google/pubs/or-tools-vehicle-routing-solver-a-generic-constraint-programming-solver-with-heuristic-search-for-routing-problems/
- Timefold Solver: https://github.com/TimefoldAI/timefold-solver
- Timefold pinning blog: https://timefold.ai/blog/continuous-planning-optimization-with-pinning
- Timefold real-time / responding to change: https://docs.timefold.ai/timefold-solver/latest/responding-to-change/responding-to-change
- Timefold field-service pinning: https://docs.timefold.ai/field-service-routing/latest/real-time-planning/real-time-planning-pinning-visits
- JSPRIT: https://github.com/graphhopper/jsprit
- PyVRP: https://pyvrp.org/, https://pubsonline.informs.org/doi/10.1287/ijoc.2023.0055
- PyVRP variants discussion: https://github.com/PyVRP/PyVRP/issues/441
- VRPy: https://github.com/Kuifje02/vrpy, https://joss.theoj.org/papers/10.21105/joss.02408
- Hexaly Optimizer: https://www.hexaly.com/, https://www.hexaly.com/pricing
- Hexaly LocalSolver 12.0 release: https://www.hexaly.com/announcement/new-release-localsolver-12-0
- Hexaly OR-Tools comparison page: https://www.hexaly.com/or-tools
- Google Route Optimization API: https://developers.google.com/maps/documentation/route-optimization/usage-and-billing
- Google Maps Platform pricing: https://mapsplatform.google.com/pricing/
- Mapbox Optimization v2 (beta): https://docs.mapbox.com/api/navigation/optimization/, https://www.mapbox.com/contact/optimization-v2-api
- HERE Tour Planning API: https://www.here.com/docs/category/tour-planning-api
- HERE Tour Planning pickup/delivery use case: https://developer.here.com/documentation/tour-planning/3.1/dev_guide/topics/use-cases/pickup-delivery.html
- GraphHopper Route Optimization: https://www.graphhopper.com/route-optimization/, https://www.graphhopper.com/pricing/
- NextBillion.ai Route Optimization: https://docs.nextbillion.ai/optimization/route-optimization-api, https://nextbillion.ai/pricing
- Routific: https://dev.routific.com/pricing
- Solvice OnRoute: https://www.solvice.io/route-optimization-api, https://www.solvice.io/pricing
- Solvice vs Google comparison: https://www.solvice.io/post/google-routes-api-alternatives-route-optimization-apis-for-2025
- NextBillion top open-source routing tools roundup: https://nextbillion.ai/blog/top-open-source-tools-for-route-optimization
- VRP solvers comparison (OR-Tools / OptaPlanner / SaaS): https://www.singdata.com/trending/comparing-vrp-solvers-ortools-optaplanner-saas/
- Spare Engine: https://spare.com/products/spare-engine
- RideCo Solver: https://www.rideco.com/differentiator/solver
- Optibus + Liftango partnership: https://blog.optibus.com/announcements/optibus-and-liftango-partner-to-deliver-integrated-fixed-route-and-on-demand-transport-platform
- Cordeau DARP improved tabu search (recent): https://arxiv.org/pdf/1801.09547
- Cordeau & Laporte DARP foundational: https://www.sciencedirect.com/science/article/abs/pii/S0191261502000450
- Dial-a-Ride with limited pickups per trip (2024): https://arxiv.org/html/2408.07602
- PyVRP+ LLM-driven HGS evolution (2025): https://arxiv.org/abs/2604.07872
