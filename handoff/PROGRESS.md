# VROOM Fork — Progress Log

The single source of truth for what has shipped on the fork. One section per milestone, appended in order. The fork's code lives on `master`; this log lives on `handoff/initial-briefing` so the project owner can read state without pulling the main branch.

---

## Milestone M0 — fork scaffolding — 2026-04-18

**Status**: complete
**Commits**: pushed to `claude/busportal-dispatch-features-W1Uwk` — `701d90f` (M0 scaffolding, 10 files, +495 LoC) and `8c1533b` (gitignore the session stash). Ready for internal review and merge into `master`. Not yet tagged.
**Upstream issue/PR**: not opened. M0 is pure scaffolding with no feature code, so the "RFC: intent to implement" policy doesn't apply. First upstream issue is owed at the start of M1.

### What shipped
- `scripts/bench.sh` — N-run solve-time harness, median/p99, CSV on stdout; accepts `VROOM_ARGS` for wiring a live Valhalla or OSRM.
- `scripts/regression.sh` — compares fork `summary.cost` / `routes` / `unassigned` vs. a recorded `solution-*.json`; exits non-zero on drift.
- `tests/fixtures/regression/` — self-contained problem/solution pair (cloned from upstream `docs/example_2.json` + `_sol`) so CI gets a real regression signal without needing a router. `tests/fixtures/regression/README.md` documents the "embedded matrix required" contract for future fixtures.
- `docker/Dockerfile` + `docker/entrypoint.sh` — two-stage image: stage 1 builds `vroom`, stage 2 bundles `vroom-express v1.5.0` and listens on `:3000`. Entrypoint picks up `/conf/config.yml` when present, matching the env-doc ConfigMap contract.
- `.github/workflows/fork-ci.yml` — build + regression + bench on every push to `master` / `claude/**` / `feat/**` and every PR into `master`. Uploads `bench.csv` as an artifact.
- `.github/workflows/docker-image.yml` — builds on every push to `master` (and relevant PRs), pushes to `registry.gitlab.itnet.lviv.ua/busportal/backend/vroom-fork` when `GITLAB_REGISTRY_USER` and `GITLAB_REGISTRY_TOKEN` are configured; builds-only when secrets are absent.
- `CHANGELOG.md` — `Unreleased → Added (Busportal fork — M0 scaffolding)` section documenting all of the above.
- `.gitignore` — ignores `.handoff-out/`, a session-local stash directory used before push auth was wired up.

**No code under `src/` or `include/` is modified.** The fork binary is byte-compatible with mainline at `master` (commit `07be776`, post-v1.15.0 / pre-v1.16.0).

### Behavioral change (consumer-visible)
None. M0 is pure scaffolding — no new JSON fields, no new endpoints, no code the consumer (`backend-dispatch`) can delete yet. The consumer continues to run against the upstream v1.13.0 image until M1 lands.

### Benchmark deltas (vs. mainline v1.13.0 baseline)
The sandbox has no Valhalla, so the Busportal handoff fixture set couldn't run. Validated only on the self-contained CI fixture:

| Fixture | Median ms (before / after) | P99 ms (before / after) | Cost delta |
|---|---|---|---|
| `example-2` (2 jobs, 1 vehicle, embedded matrix) | N/A / 3 ms | N/A / 5 ms | 0 (5461 → 5461) |

"Before" is N/A because no v1.13.0 binary was available in the sandbox. **Action for M1**: before merging any feature, run `./scripts/bench.sh tests/fixtures/regression` on both a v1.13.0 (or v1.15.0) checkout and the fork's HEAD, and commit both CSVs as baselines so every later milestone has a real number to regress against.

### Risks and open items
- **Busportal handoff fixtures can't run in local CI yet.** They rely on live Valhalla. The self-contained `tests/fixtures/regression` fixture is enough to catch "did we break mainline VROOM" but it's a 2-job problem — the real workload is 10–50 shipments with co-located stops. Before M3 (F1 co-located shared-stop batching) lands, capture and commit matrices for the three `problem-shipments-*.json` fixtures (Option B from `handoff/vroom-fork-bench.md` — Python stub returning canned matrices — is enough for ~90% of cases).
- **Docker registry secrets.** `GITLAB_REGISTRY_USER` / `GITLAB_REGISTRY_TOKEN` not yet configured on the `gvoider/vroom` GitHub repo. Until they are, `docker-image.yml` builds the image in CI but doesn't push, so K8s has nothing new to pull. Needs a GitLab deploy token (`write_registry` on `busportal/backend/vroom-fork`) plus both secrets on the GitHub repo before M1 tagging.
- **Signed-commit configuration.** Harness git config sets `commit.gpgsign=true` but the signing key `/home/claude/.ssh/commit_signing_key.pub` is a zero-byte file, so commits are technically signed with an empty key. GitHub accepts the push; strict verification would fail. Either populate the key or disable `commit.gpgsign` for harness sessions.
- **Upstream RFC issue.** The handoff policy asks for an upstream issue per feature on `VROOM-Project/vroom`. M0 doesn't need one; M1's first action is to open *"RFC: expose per-objective cost breakdown in `summary.cost_breakdown`"*. That's the next gate before M1 coding starts.
- **Branch model.** Current harness pins work to `claude/busportal-dispatch-features-W1Uwk` rather than one branch per milestone (`feat/m1-*`, `feat/m2-*`, …). Either loosen that pin for future sessions or accept milestone-tagged commits on the same feature branch. RFC §9 prefers per-milestone branches — worth deciding before M1.

### Notes for next milestone
- **M1 = F3 per-objective cost breakdown.** Pure accounting (no algorithmic change), ~1 week estimate per RFC §7.
- Pre-work before writing C++:
  - Open upstream issue *"RFC: structured cost breakdown in `summary.cost_breakdown`"* on `VROOM-Project/vroom`.
  - Re-read RFC §4.3 (input=none, output extends `summary` with `cost_breakdown` and `routes_cost_breakdown`).
  - Grep `src/structures/vroom/cost_wrapper.cpp`, `src/structures/vroom/solution/summary.cpp`, `src/utils/output_json.cpp` to find where components are already accumulated and where the summary JSON is assembled.
- Implementation shape: a `cost_components_t` struct alongside the existing accumulated cost; write each component as it is accrued; expose via `write_breakdown(rapidjson::Value&)` in `output_json.cpp`.
- Acceptance gate per RFC §4.3.5: components sum to `summary.cost` within ±1 unit; no solve-time regression > 5%; consumer PR in `backend-dispatch` consumes the new field behind a feature flag.
- Pre-merge sanity check on every feature branch: `make -C src -j && ./scripts/regression.sh tests/fixtures/regression && ./scripts/bench.sh tests/fixtures/regression`.

---

## Milestone M1 — F3 per-objective cost breakdown — 2026-04-18

**Status**: complete
**Commits**: pushed to `claude/busportal-dispatch-features-W1Uwk` — `5f9932e6` ("M1 (F3): per-objective cost breakdown in solution output", 20 files, +348 / −28 LoC).
**Upstream issue/PR**: not opened. GitHub MCP scope in the session is restricted to `gvoider/vroom`, so the RFC issue on `VROOM-Project/vroom` is deferred. Owner can open manually: suggested title *"RFC: expose per-objective cost breakdown in `summary.cost_breakdown`"*.

### What shipped
- `src/structures/vroom/solution/cost_breakdown.h` — new `CostBreakdown` struct with buckets `fixed_vehicle`, `duration`, `distance`, `task`, `priority_bias`, `soft_time_window_violation`, `published_vehicle_deviation`. Additive via `operator+=`.
- `src/structures/vroom/solution/route.h` + `summary.h` — `CostBreakdown cost_breakdown` field added alongside `cost`.
- `src/structures/vroom/solution/solution.cpp` — accumulates `summary.cost_breakdown += route.cost_breakdown`.
- `src/structures/vroom/cost_wrapper.h` — new `per_hour()` / `per_km()` accessors so the breakdown can split travel cost the same way `user_cost_from_user_metrics` does.
- `src/utils/helpers.{h,cpp}` — new `utils::compute_route_cost_breakdown()` called at the CVRP and VRPTW `Route`-construction sites; handles both the `cost_based_on_metrics` case (splits travel cost into `duration` and `distance` buckets) and the user-supplied-cost-matrix case (parks travel cost in `duration`, leaves `distance` at zero).
- `src/algorithms/validation/choose_ETA.cpp` — same breakdown computation at the VRPTW validation `Route` construction site.
- `src/utils/output_json.{h,cpp}` — new `to_json(const CostBreakdown&, …)` overload; `cost_breakdown` emitted on both `summary` and each `routes[]` entry.
- `scripts/regression.sh` — now asserts the breakdown sum invariant (`|cost − sum(breakdown)| ≤ 1` for every route and for the summary); fails CI on drift.
- `tests/fixtures/regression/problem-cost-breakdown.{json}` + solution — exercises non-zero `fixed_vehicle`, `duration`, `distance`, `task` buckets.
- `tests/fixtures/regression/problem-custom-cost-matrix.{json}` + solution — exercises the `!cost_based_on_metrics` path where a user-supplied `costs` matrix makes distance-vs-duration attribution meaningless.
- `docs/API.md` — new "Cost breakdown" subsection documenting keys, invariants, and the forward-looking placeholders.
- `CHANGELOG.md` — Unreleased → M1 entry.
- `bench-baselines/bench-baseline-post-m0.csv` and `bench-post-m1.csv` — numeric baselines committed; bench.sh remains the canonical way to regenerate.

### Behavioral change (consumer-visible)
Every solve now returns:

```json
"summary": {
  "cost": …,
  "cost_breakdown": {
    "fixed_vehicle": …, "duration": …, "distance": …, "task": …,
    "priority_bias": 0,
    "soft_time_window_violation": 0,
    "published_vehicle_deviation": 0
  },
  …
},
"routes": [
  { "vehicle": …, "cost": …, "cost_breakdown": { … }, … }
]
```

All other fields are untouched. `backend-dispatch` can start consuming `cost_breakdown` behind a feature flag; no existing field is renamed or removed.

### Benchmark deltas (vs. post-M0 baseline on the self-contained fixture)

| Fixture | Median ms (before / after) | P99 ms (before / after) | Cost delta |
|---|---|---|---|
| `example-2` | 4 / 3 | 8 / 5 | 0 (5461 → 5461) |
| `cost-breakdown` (new) | — / 0 | — / 1 | — |
| `custom-cost-matrix` (new) | — / 0 | — / 0 | — |

No regression on `example-2`. Run-to-run noise dominates at this fixture size. Real regression check requires a larger problem set — see "Valhalla mock" blocker below.

### Risks and open items
- **Valhalla mock still not set up.** Same blocker as after M0. Large-problem benchmarking waits on mock matrices or a dev Valhalla instance. M3 (F1 co-located shared-stop batching) will materially need this; M1–M2 don't.
- **Docker registry secrets still not configured.** Same blocker as after M0. The image builds in CI but won't publish until `GITLAB_REGISTRY_USER`/`GITLAB_REGISTRY_TOKEN` are added.
- **Upstream RFC issue owed.** Not opened from this session (MCP scope). Either the owner opens it manually with the title above, or a future session with `public_repo` PAT scope handles it before any upstream PR.
- **`distance` bucket behavior when using a custom `cost` matrix.** We park the full travel cost in `duration` and leave `distance` at 0, which is documented but may surprise consumers that inspect the split. If the dispatcher UI cares, add an `attribution: "from_metrics" | "from_cost_matrix"` tag on the breakdown — deferred pending UAT feedback.
- **No C++ unit tests yet.** Upstream has no catch2 harness in this repo; we rely on JSON-level regression fixtures. If M3+ accumulate enough non-trivial logic, introduce catch2 as a new `tests/unit/` subdir — but that's a scope call for M3 prep, not M1.

### Notes for next milestone
- **M2 = F5 structured unassigned-reason diagnostics** (~1 week per RFC §7). Swaps the current coarse `unassigned[].reason` (string or absent) for a structured reason code object per RFC §4.5. Key consumer deletion: `backend-dispatch DispatchDiagnostician::classifyOne` (≈50 LoC).
- Pre-work before writing C++:
  - Open upstream issue *"RFC: structured `unassigned[i].reason` with stable reason codes"* on `VROOM-Project/vroom`.
  - Re-read RFC §4.5 for the codebook: `no_route_found`, `capacity_exceeded`, `time_window_miss`, `skills_missing`, `max_tasks_exceeded`, `max_travel_time_exceeded`, `invalid`, `other`.
  - Find where `unassigned` is populated today. Start with `src/utils/helpers.cpp` (`get_unassigned_jobs_from_ranks`) and trace backward to the rejection sites in `src/problems/*/` and the local-search rollback paths — those are the places that KNOW the reason but currently drop the information.
- Implementation shape: add `UnassignedReason` enum + `std::string` helper; extend `Job` (or a sibling `UnassignedJob`) with an optional reason code + freeform detail; emit as `{ "reason": "capacity_exceeded", "detail": "pickup exceeds vehicle 3 capacity by 2" }`.
- Sanity check on every feature branch: `make -C src -j && ./scripts/regression.sh tests/fixtures/regression && ./scripts/bench.sh tests/fixtures/regression`.

---

## Inbox directive — issue #2 "Read handoff branch and continue" — 2026-04-18

**Status**: complete (directive satisfied; issue left open because PAT lacks the scope to close it or comment)
**PR**: no code change — directive was to read protocol and continue milestone work.
**Summary**: Read `handoff/AGENT-PROTOCOL.md` (incl. the refinement at `a7848cfe`), rebased this branch onto the new tip, and shipped M0 + M1 per the RFC plan. Attempted to close the issue with a summary comment per protocol rule 4; both `mcp__github__add_issue_comment` and direct PATCH via the session PAT returned `Resource not accessible by personal access token` / `by integration`. Owner action needed to grant Issues: Read and write scope to the fine-grained PAT so future sessions can close inbox issues themselves.

---

## Milestone M2 — F5 structured unassigned-reason diagnostics — 2026-04-18

**Status**: complete
**Commits**: pushed to `claude/busportal-dispatch-features-W1Uwk` — `c8d1521d` ("M2 (F5): structured unassigned-reason diagnostics", 27 files, ~+620 LoC net).
**Upstream issue/PR**: not opened — same MCP-scope blocker as M1. Suggested title: *"RFC: structured `unassigned[i].reason` with stable reason codes"*.

### What shipped
- `src/structures/vroom/solution/unassigned_info.h` — `UnassignedReason` enum (`no_vehicle_with_required_skills`, `capacity_exceeded`, `time_window_infeasible`, `max_travel_time_exceeded`, `route_duration_limit_exceeded`, `no_feasible_insertion`) + `UnassignedDetails` struct (optional per-code fields) + `to_string()` helper.
- `src/utils/unassigned_classifier.{h,cpp}` — `utils::classify_unassigned(Input&, unassigned_jobs)` returns a parallel `std::vector<UnassignedInfo>`. Checks fire in the order above; the first one that eliminates every vehicle wins.
- `Solution::unassigned_info` populated only when `Input::diagnostics()` is on. Both CVRP and VRPTW `format_solution` paths call the classifier.
- CLI `-d` / `--diagnostics` flag wired through `cl_args.h`, `main.cpp`, `io::parse`, and `Input::set_diagnostics()`.
- `to_json(const UnassignedDetails&, …)` overload in `output_json.{h,cpp}`; `reason` + `details` emitted on each `unassigned[]` entry only when diagnostics are on.
- `scripts/regression.sh` now supports `fixtures/diagnostics/<label>.json` expectation files: when present, re-runs with `-d` and asserts per-id reason codes.
- Three new self-contained regression fixtures with matching diagnostics expectations: `problem-unassigned-capacity.json` (`capacity_exceeded`), `problem-unassigned-skills.json` (`no_vehicle_with_required_skills`), `problem-unassigned-tw.json` (`time_window_infeasible`).
- `docs/API.md` — new "Unassigned reasons" subsection.
- `CHANGELOG.md` — Unreleased → M2 entry.
- `bench-baselines/bench-post-m2.csv` — no regression on the pre-existing fixtures; the three new unassigned fixtures run ≤ 9 ms p99.

### Behavioral change (consumer-visible)
With `-d`, each unassigned entry gains:

```json
{
  "id": 42, "type": "pickup",
  "reason": "time_window_infeasible",
  "details": {
    "earliest": 140100, "latest": 140400,
    "closest_feasible_vehicle": 3,
    "closest_feasible_arrival": 141000,
    "shortfall_seconds": 600
  }
}
```

Without `-d`, output shape is byte-identical to mainline. Consumer opts in per-request, matching the RFC's `?diagnostics=full` contract.

Consumer unlock: `backend-dispatch DispatchDiagnostician::classifyOne` (~80 LoC PHP string-matching) can be reduced to a thin reason-code → user-hint translator (~30 LoC) in the next consumer PR.

### Benchmark deltas (vs. post-M1 baseline)

| Fixture | Median ms (before / after) | P99 ms (before / after) |
|---|---|---|
| `example-2` | 3 / 3 | 5 / 6 |
| `cost-breakdown` | 0 / 0 | 1 / 0 |
| `custom-cost-matrix` | — / 0 | — / 1 |
| `unassigned-capacity` (new) | — / 7 | — / 9 |
| `unassigned-skills` (new) | — / 7 | — / 8 |
| `unassigned-tw` (new) | — / 7 | — / 9 |

Classifier overhead is bounded by `O(|unassigned| × |vehicles|)` matrix lookups — trivial at Busportal's scale (10–50 shipments, 1–3 vehicles). The ≤ 9 ms on the new fixtures is dominated by solver setup, not classification.

### Risks and open items
- **Valhalla mock still not set up.** Same blocker carried from M0/M1. Doesn't bite M2; will bite M3.
- **Docker registry secrets.** Still absent; `docker-image.yml` still builds-only.
- **Upstream RFC issues.** Two owed (M1 + M2). Same PAT/scope constraint.
- **`no_feasible_insertion` is a truthful fallback but a weak signal.** The classifier says "job could fit on at least one vehicle alone but the full search couldn't place it." A proper diagnosis would surface which candidate routes would have accepted the job absent competition. Deferred pending dispatcher UAT — if this code shows up too often to be useful, add a "best_alternative_vehicle" hint in a follow-up.
- **HTTP opt-in path.** The `-d` flag works at CLI. `vroom-express` (the HTTP wrapper, separate upstream repo) needs a small change to pass `diagnostics: true` from request body through to the binary. Flagged for the consumer-integration PR; not blocking the M2 merge.
- **Time-window classification pivots on the vehicle's start TW.** Deployments that rely on `steps[]` or mid-route waypoints for ETA feasibility aren't modelled. All currently-failing such paths fall through to `no_feasible_insertion` — correct but less informative. Follow-up if UAT asks.

### Notes for next milestone
- **M3 = F1 native co-located shared-stop batching** (~2.5 weeks per RFC §7 — the single biggest milestone). Deletes ~540 LoC of consumer code (`DispatchSharedPickupBatcher.php` + peel-off loop).
- Pre-work before writing C++:
  - Open upstream issue *"RFC: native co-located shared-stop batching"* on `VROOM-Project/vroom`.
  - Re-read RFC §4.1 carefully — this is an algorithmic feature, not accounting. Changes how routes cost the *same* stop visited by multiple co-located pickups (charge service time once, keep arrival times equal within the group).
  - Identify where `service` time flows into route cost (`vehicle::task_cost()` consumers in `helpers.cpp` + `choose_ETA.cpp`); where arrival times are constrained equal within a step list; where new-job insertion operators would need to know two jobs "share" a stop.
  - Mock Valhalla matrices FIRST so we can bench the 10–50-shipment workload before landing algorithmic changes. The sandbox has no routing backend; without bench numbers we can't claim ≤ 500 ms median per the RFC performance floor.
- Implementation shape: add optional `co_located_group` string to the pickup schema; at route-cost time, for each vehicle's route identify groups of steps sharing a group string and charge `max(service)` once per group instead of summing; enforce "same `co_located_group` implies same location within 1.1 m" at input validation.
- Acceptance gate per RFC §4.1.7: median solve time on 30-shipment problems with 50% co-located stays under 500 ms; `co_location_savings_seconds` on `summary.computing_times` matches PHP-computed savings on regression fixtures.
