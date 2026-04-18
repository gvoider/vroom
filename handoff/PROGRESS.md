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
