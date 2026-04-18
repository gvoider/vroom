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

---

## Inbox directive — issue #3 "Dockerfile: 5 build/runtime fixes found during L1 smoke test" — 2026-04-18

**Status**: code fixes complete on branch; PR + issue-close blocked by PAT scope
**PR**: not opened — PAT lacks Pull requests: Read and write. Branch pushed to `origin/agent/issue-3-dockerfile-fixes` (commit `fc3eccdd`). PR template URL: https://github.com/gvoider/vroom/compare/claude/busportal-dispatch-features-W1Uwk...agent/issue-3-dockerfile-fixes (owner must open manually). PR base should be `claude/busportal-dispatch-features-W1Uwk`, not `master`, because `docker/Dockerfile` lives on the feature branch until M0→M2 merge to master.
**Summary**: Applied all six fixes from cyril's L1-smoke-test report verbatim. (1) base image bookworm-slim → trixie-slim for GCC 13+ (std::format); (2) VROOM_EXPRESS_VERSION v1.5.0 → v0.12.0 (real tag); (3) HUSKY=0 + --ignore-scripts on npm install; (4) new `.gitattributes` + `sed -i 's/\r$//'` belt-and-braces for CRLF-safe shebangs; (5) chown /opt/vroom-express to the unprivileged vroom user; (6) entrypoint now copies `/conf/config.yml` to `/opt/vroom-express/config.yml` (Option A) because vroom-express ignores `VROOM_CONFIG`. CHANGELOG.md Unreleased section documents all six under "Fixed". Sandbox has no Docker daemon so end-to-end build wasn't re-verified; CI's docker-image.yml is the first real integration surface.

**Blocker (for both #2 and #3)**: the fine-grained PAT in the harness has Contents: Read+Write but no Issues: R+W and no Pull requests: R+W. Every non-git-push write to the GitHub API returns `Resource not accessible`. Adding those two scopes lets future sessions open PRs and close inbox issues directly per `handoff/AGENT-PROTOCOL.md` rule 4. Until then I can push branches and commits but the owner owns the PR open/close/comment steps.

---

## Milestone M3 — F1 native co-located shared-stop batching — 2026-04-18

**Status**: complete, merged, tagged
**Commits**: `feat/m3-co-located` → `master` via PR #6, merge commit `73cbeee6`. Tag `v1.15.0-busportal.m3` points at the same commit.
**Upstream issue/PR**: not opened (fine-grained PAT still can't target `VROOM-Project/vroom`). Owner action if/when an upstream PR is desired.

### What shipped
- `Job` and `Step` gain an optional `co_located_group` string. Empty / absent = mainline behavior.
- `Input::check_co_located_groups()` validates "same non-empty tag ⇒ same location within ~1.1 m" and rejects mismatches with `code: 2`.
- `utils::apply_co_location_dedup()` (new file `src/utils/co_location_dedup.{h,cpp}`) runs as a post-solve accounting pass:
  - For each route, scans for maximal consecutive runs of pickup steps sharing the tag.
  - Equalizes arrival times across the run, keeps `max(service)` on the first member, zeros the rest, reduces `route.service`, `route.cost`, and `route.cost_breakdown.task` by the saving.
  - Aggregates across routes into `summary.computing_times.co_location_savings_seconds` (named `_seconds` because the rest of `computing_times` is milliseconds — intentional).
- Input parser reads `pickup.co_located_group` and threads it into the `Job` constructor.
- Step JSON emission unchanged externally (no new output-only field — the tag is input-only from the consumer's perspective; the behavior shows up as equalized `arrival` and zeroed `service` on later members).
- Two new self-contained regression fixtures:
  - `problem-co-located-group.json` — 3 pickups at stop A + 1 at stop B on one vehicle; saving = 420 s from `max(300,240,180)` dedup.
  - `problem-co-located-split.json` — capacity forces `pu-pu-del-del-pu-pu-del-del`; per-run dedup; saving = 360 s.
- Three `problem-embedded-shipments-N` fixtures staged from `handoff/vroom-fork-fixtures/` so CI has real-Lviv-matrix regression coverage offline. Matching solutions recorded.
- `docs/API.md` — new `co_located_group` subsection under the shipment-step table.
- `CHANGELOG.md` — Unreleased → M3 entry.
- `bench-baselines/bench-baseline-pre-m3.csv` + `bench-post-m3.csv`.
- Bonus fix: `docker-image.yml` tag computation (`${{ github.ref_name }}` is `<N>/merge` on PR events; Docker tags disallow slashes). Now emits the sha tag always and a slash-flattened symbolic tag only on push events.

### Behavioral change (consumer-visible)
Shipment pickup step gains optional `co_located_group: string`. When ≥ 2 pickups share the non-empty tag and end up consecutive on one vehicle:

```json
"steps": [
  { "type": "pickup", "id": 11, "arrival": 600, "service": 300 },
  { "type": "pickup", "id": 12, "arrival": 600, "service": 0 },
  { "type": "pickup", "id": 13, "arrival": 600, "service": 0 },
  …
]
```

plus `summary.computing_times.co_location_savings_seconds: 420`. Absent tag ⇒ mainline behavior. Mismatched locations under the same tag ⇒ input-error 400.

Consumer unlock: `backend-dispatch` can delete `DispatchSharedPickupBatcher.php` (~460 LoC) and the peel-off loop in `VroomClient::solveIteratively` (~80 LoC) as flagged in the RFC consumer-hooks doc.

### Benchmark deltas (post-M3 vs pre-M3 baseline on self-contained fixtures)

| Fixture | Median ms (before / after) | P99 ms (before / after) |
|---|---|---|
| `example-2` | 3 / 3 | 4 / 4 |
| `embedded-shipments-3` | 6 / 7 | 9 / 10 |
| `embedded-shipments-4` | 7 / 7 | 7 / 8 |
| `embedded-shipments-5` | 7 / 7 | 9 / 8 |
| `co-located-group` (new) | — / 7 | — / 8 |
| `co-located-split` (new) | — / 7 | — / 9 |

All well under the 500 ms / 30-shipment budget. The sandbox only has up to 5-shipment fixtures — the real 10–50-shipment workload benches on CI / L1.

### Risks and open items
- **Solver doesn't proactively chase co-location bonus.** Local search clusters same-location pickups naturally because travel cost between them is 0, but there's no explicit cost-term bonus in the objective. If UAT reveals the solver sometimes disperses group members it should keep together, add the bias as a cheap follow-up — RFC §4.1.3 explicitly calls this "no explicit new cost term needed" so we respected that.
- **Upstream RFC issues for M1, M2, and M3 still owed.** Fine-grained PAT in the harness can only target `gvoider/vroom`. Owner owns these if upstream contribution is desired.
- **Docker registry secrets still absent.** `docker-image.yml` builds cleanly now (bonus fix above) but still doesn't push to GitLab until `GITLAB_REGISTRY_USER`/`TOKEN` are set on the repo.
- **`co_location_savings_seconds` name.** The RFC calls for it on `computing_times` even though the rest of that struct is in ms. Kept the RFC naming with an explicit `_seconds` suffix so consumers don't mix units.

### Notes for next milestone
- **M4 = F2 soft time windows** (~1 week per RFC §7). Adds `soft_time_window: {preferred, cost_per_second_before, cost_per_second_after}` to any TW-bearing step; the solver biases arrivals toward the preferred sub-interval via a linear cost term.
- Pre-work before writing C++:
  - Open upstream issue *"RFC: soft time windows via preferred sub-interval"* on `VROOM-Project/vroom` (owner, since fine-grained PAT can't).
  - Re-read RFC §4.2. This IS a cost-function change — unlike M3, this one adds an explicit term to the objective, so the solver actually chases it. Less post-hoc, more algorithmic.
  - Identify where arrivals are evaluated relative to hard TWs (`tw_route`, scheduling logic in `problems/vrptw/`) — the soft cost needs to slot in alongside lateness / earliness checks without breaking hard-TW enforcement.
- Implementation shape: extend `TimeWindow` with optional `preferred_start`, `preferred_end`, `cost_per_second_before`, `cost_per_second_after`; during route evaluation add `soft_time_window_violation` to `cost_breakdown` (the field already exists as a placeholder from M1); emit per-step `soft_window_violation_cost` in solution JSON.
- Acceptance gate per RFC §4.2.7: consumer deletes `VroomClient::shiftRoutesLate` (~55 LoC); no > 10 % regression in median solve time.
- Pre-merge sanity on every feature branch: `make -C src -j && ./scripts/regression.sh tests/fixtures/regression && ./scripts/bench.sh tests/fixtures/regression`.

---

## Inbox directive — issue #5 "M3 unblocked — embedded Valhalla matrices now available; proceed with F1" — 2026-04-18

**Status**: complete
**PR**: #6 (merged, `73cbeee6`); tag `v1.15.0-busportal.m3`
**Summary**: Picked up master at `149711b1`, staged the three embedded-shipments fixtures into `tests/fixtures/regression/`, captured the pre-M3 baseline, implemented F1 with minimal scope (input parsing + validation + post-solve arrival-equalization / service dedup), added two new F1 fixtures, patched docs + CHANGELOG, opened PR into master, fixed one CI-surfaced docker-tag bug, self-merged on green, tagged `v1.15.0-busportal.m3`. Full details in the M3 milestone section above.

---

## Inbox directive — issue #7 "M3 post-merge follow-up: 4 correctness fixes before M4 starts" — 2026-04-18

**Status**: complete, merged, tagged
**PR**: #8 (merged, `4fe36782`); tag `v1.15.0-busportal.m3.1`
**RFC amendment**: `3dbd0f97` on `handoff/initial-briefing` — §4.1.3 clarified for per-run equalization

### What shipped
- **Bug 1 fix**: `step.duration` is cumulative *travel* per VROOM upstream; only `step.arrival` shifts during dedup. `scripts/regression.sh` now asserts `arrival[k] - arrival[0] == duration[k] + cumulative(setup+service+waiting) before k` on every fixture, so any future desync fails CI immediately.
- **Bug 2 fix**: `common_arrival = max(anchor.arrival, max(member.tws.front().start))` so equalized arrivals never violate any member's own earliest-start TW. New fixture `problem-co-located-tw-stagger.json` exercises it.
- **Pooled-service placement** (tied to bug 1): pooled `max(service)` now sits on the LAST member of a run, not the first. Every intra-run member reports `service: 0` at the shared arrival, keeping the step-timing invariant intact. Required re-recording the `co-located-{group,split}` solutions.
- **Bug 3 fix**: RFC §4.1.3 amended to clarify that equalization applies within a maximal consecutive run, not group-as-a-whole. VROOM's local search legitimately interleaves groups across deliveries when capacity forces it; rejecting such solutions would eliminate otherwise-feasible answers. `docs/API.md` mirrors the amendment.
- **Bug 4 fix**: every field of `summary.cost_breakdown` is re-accumulated from routes via a single helper after any dedup adjustment. Previously only `task` was updated; other buckets held stale pre-dedup aggregates. Worked by accident on M3 fixtures because `task` was the only non-zero bucket. New fixture `problem-co-located-breakdown-reaccum.json` exercises multi-bucket breakdown with dedup.

Two new fixtures + re-records of two existing solutions. All 13 fixtures pass with the new invariant assertion. Bench `bench-post-m3.1.csv` shows no regression beyond sandbox noise.

### Non-blocking observations deferred per #7
- 30-shipment synthetic fixture for the RFC §8 performance gate: deferred to before M4 lands.
- Upstreamability note (pass reads `v.costs.per_task_hour`): captured in CHANGELOG for when the upstream RFC is eventually filed.

### Notes for next milestone
- M3.1 is a pure correctness-hardening release; M4 (F2 soft time windows) picks up where the M3 PROGRESS entry left off. No new prerequisites introduced.
- Consumer integration for M3 still pending (delete `DispatchSharedPickupBatcher.php` and the peel-off loop); now safer to do because the step-timing invariant is enforced in CI.

---

## Milestone M4 — F2 soft time windows — 2026-04-18

**Status**: complete, merged, tagged
**Commits**: PR #10 merged at `1061e639` on `master`; tag `v1.15.0-busportal.m4`.
**Upstream RFC/PR**: not opened (same fine-grained PAT scope blocker as M1–M3).

### What shipped
- `SoftTimeWindow` struct at `src/structures/vroom/soft_time_window.{h,cpp}`; `violation_cost(arrival)` helper computes the linear before/after penalty.
- `Job` gains an optional `soft_time_window`; input parser reads it on pickup/delivery/single-job steps.
- `Input::check_soft_time_windows()` rejects `preferred` not contained in any hard TW with `code: 2`.
- `utils::apply_soft_time_window_pass()` at `src/utils/soft_time_window_pass.{h,cpp}` — post-solve pass. Backward-computes latest-feasible arrivals, walks forward shifting each soft-TW step toward its preferred interval, attributes each delay increment to the PRECEDING step's `waiting_time` so the M3.1 step-timing invariant keeps holding.
- `Step` carries a copy of the Job's `soft_time_window` + new per-step `soft_window_violation_cost` field; JSON emits the latter only on steps that carry a soft TW (mainline fixtures stay byte-identical).
- `cost_breakdown.soft_time_window_violation` (zero placeholder since M1) now populated; re-accumulated onto summary via the M3.1 helper spirit.
- Three new F2 regression fixtures (`soft-tw-shift-late`, `soft-tw-before-preferred`, `soft-tw-after-preferred`) + one validation fixture (`tests/fixtures/validation/problem-soft-tw-reject.json`) for the `preferred ⊄ hard` rejection path.
- Deferred-from-M3.1 bench coverage: `scripts/gen-synthetic-30.py` + `tests/fixtures/regression/problem-synthetic-30.json`. Deterministic 28-shipment fixture (20 + 5 co-located + 3 confirmed, seeded RNG); solves in ~77 ms median / 93 ms p99, well under the 500 ms RFC §8 budget.
- `docs/API.md` — new "Soft time window" subsection.
- `CHANGELOG.md` — Unreleased → M4 entry.
- `bench-baselines/bench-post-m4.csv`.

One subtle bug caught mid-implementation (documented in PR body): pickup and delivery share the shipment id, so a naive lookup across (pickup_id_to_rank, delivery_id_to_rank) on a delivery step returned the pickup's hard TW and shifted arrivals past the delivery's own window. Fix: dispatch on `step.job_type`.

### Behavioral change (consumer-visible)

```json
"pickup": {
  "id": 42,
  "time_windows": [[140100, 140700]],
  "soft_time_window": {
    "preferred": [140400, 140500],
    "cost_per_second_before": 0.1,
    "cost_per_second_after": 0.5
  }
}
```

Output additions on the step: `soft_window_violation_cost`. On summary / route: `cost_breakdown.soft_time_window_violation` is now non-zero when any soft TW fires.

Consumer unlock: `backend-dispatch VroomClient::shiftRoutesLate` (~55 LoC) can be deleted. Tag soft pickups with the new field; fork handles the shift natively.

### Benchmark deltas (post-M4 vs pre-M4, self-contained fixtures)

| Fixture | Median ms (before / after) | P99 ms (before / after) | Cost delta |
|---|---|---|---|
| `example-2` | 3 / 4 | 4 / 6 | 0 |
| `embedded-shipments-3` | 9 / 9 | 10 / 11 | 0 |
| `embedded-shipments-5` | 9 / 9 | 11 / 11 | 0 |
| `co-located-group` | 7 / 9 | 8 / 11 | 0 |
| `soft-tw-shift-late` (new) | — / 9 | — / 11 | — |
| `soft-tw-before-preferred` (new) | — / 10 | — / 13 | — |
| `soft-tw-after-preferred` (new) | — / 9 | — / 11 | — |
| `synthetic-30` (new, 28 shipments) | — / 77 | — / 93 | — |

No regression beyond sandbox noise. RFC §8 acceptance gate (500 ms median on 30-shipment problems) holds with an 80 ms margin.

### Risks and open items
- **Shift-late only.** The pass never pulls an arrival earlier than the solver's choice. A step already past `preferred_end` pays `cost_per_second_after` but isn't re-sequenced. If UAT reveals that mixed tight/loose routes need bidirectional shift, that's an M4.1 follow-up.
- **Solver doesn't see the soft cost during sequence selection.** Post-solve accounting only. If UAT shows the solver is picking sub-optimal sequences wrt soft cost, add a GLPK LP-objective term in choose_ETA.cpp as a follow-up. Low-probability issue for Busportal's workload (all-soft-pickups case works; mixed case largely works because the tight step's hard TW already pins the sequence).
- **Upstream RFC for F2 still owed.** Same PAT-scope blocker.
- **Docker registry secrets still absent.** Still non-blocking; re-examine at M5.

### Notes for next milestone
- **M5 = F4 plan diff endpoint** (~0.5 weeks per RFC §7). New `POST /diff` HTTP endpoint taking two solution JSONs and returning a structured diff. This is the first milestone that touches the HTTP surface — most of the work lives in `vroom-express` (separate upstream repo) rather than our fork.
- Pre-work:
  - Open upstream RFC (same PAT blocker; owner-side).
  - Re-read RFC §4.4 — shape of the diff payload, supported change types.
  - Decide split: does the diffing logic live in our fork (new helper) or entirely in `vroom-express`? RFC §4.4 suggests the fork, so `backend-dispatch` just POSTs the two solutions and reads back a structured diff. Implication: our fork image needs `vroom-express` configured to route `/diff` to a new handler, which means a `vroom-express` PR too.
- Pre-merge sanity: `make -C src -j && ./scripts/regression.sh tests/fixtures/regression && ./scripts/bench.sh tests/fixtures/regression`.

---

## Inbox directive — issue #9 "M4 kickoff — F2 soft time windows" — 2026-04-18

**Status**: complete
**PR**: #10 (merged, `1061e639`); tag `v1.15.0-busportal.m4`
**Summary**: Full M4 shipped on `feat/m4-soft-time-windows` → merged to master. Three new F2 regression fixtures + one validation-rejection fixture + the deferred 28-shipment synthetic bench. Details in the M4 milestone section above.
