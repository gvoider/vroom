# VROOM Fork — Initial Briefing

You landed on this branch because you've been asked to work on this VROOM fork
(`gvoider/vroom`) to add domain-specific features for Busportal's passenger-transfer
dispatch service. This is an **orphan handoff branch** — it has no code, only docs.
The code lives on `main`.

## Read in this order

1. **[handoff/vroom-fork-rfc.md](handoff/vroom-fork-rfc.md)** — the main requirements
   document. §0 tells you how to pick up the work. Start here.
2. **[handoff/vroom-fork-env.md](handoff/vroom-fork-env.md)** — deployment and
   environment facts (image tag, config, Valhalla, K8s contract).
3. **[handoff/vroom-fork-glossary.md](handoff/vroom-fork-glossary.md)** — domain
   terms. Keep open in a tab while reading the RFC.
4. **[handoff/vroom-fork-consumer-hooks.md](handoff/vroom-fork-consumer-hooks.md)** —
   exact files/lines in the downstream consumer (`backend-dispatch`) that change
   per milestone. Look at this when working on a specific milestone.
5. **[handoff/vroom-fork-bench.md](handoff/vroom-fork-bench.md)** — benchmark
   harness (scripts, budgets, CI integration).
6. **[handoff/vroom-fork-fixtures/README.md](handoff/vroom-fork-fixtures/README.md)** —
   real VROOM problem/solution pairs captured from our dev environment.

## Deep-dive research (only if you want the "why")

- **[reports/dispatch-stability-research.md](reports/dispatch-stability-research.md)** —
  the "chaotic shuffle" complaint and how the industry solves it (Spare, Via,
  ConVRP literature).
- **[reports/vroom-alternatives-research.md](reports/vroom-alternatives-research.md)** —
  why we picked fork over Timefold / OR-Tools / rewrite.
- **[reports/vroom-workarounds-inventory.md](reports/vroom-workarounds-inventory.md)** —
  the 33 workarounds and ~1800 LoC of hacks the fork is meant to replace.

## Scope summary

Fork five P0 features (and three P1 features if on schedule):

| ID | Feature | Deletes | Priority |
|----|---------|---------|----------|
| F1 | Native co-located shared-stop batching | ~540 LoC consumer | **P0** |
| F2 | Soft time windows | ~55 LoC consumer | **P0** |
| F3 | Per-objective cost breakdown | 0 LoC (adds diagnostics) | **P0** |
| F4 | `POST /diff` endpoint | 0 LoC (adds UX support) | **P0** |
| F5 | Structured unassigned-reason codes | ~50 LoC consumer | **P0** |
| F6 | `POST /counterfactual` endpoint | 0 LoC | P1 |
| F7 | Driver shifts + breaks | 0 LoC (new capability) | P1 |
| F8 | Published-vehicle soft stability penalty | 0 LoC (new capability) | P1 |

Total P0: ~6.5 weeks of focused work. P0 + P1: ~10 weeks.

See the RFC §7 for the milestone schedule.

## How to work

- Work on the fork's `main` branch (where VROOM's code lives).
- One feature branch per milestone: `feat/mN-description`.
- Before each feature: open an RFC-style issue on **upstream** VROOM
  (https://github.com/VROOM-Project/vroom/issues) to gauge maintainer interest.
  If welcomed, PR upstream after landing on our fork. If not, keep on our fork only.
- Open PRs against `gvoider/vroom:main` for internal review before tagging a
  milestone release.
- Do NOT merge this `handoff/initial-briefing` branch into `main`. It's a
  read-only briefing, nothing more.

## Getting help

The project owner is `cyril@biryulov.net`. Technical questions about the
downstream consumer (`backend-dispatch`) or dispatcher UX go there. Routing-algorithm
questions can go to the upstream VROOM maintainers via GitHub issues.

## What's NOT on this branch

- The consumer code (`backend-dispatch`). It lives on GitLab. Exact integration
  points documented in `handoff/vroom-fork-consumer-hooks.md`.
- Production secrets. They'll be provided out of band when you need to deploy.
- K8s cluster access. You don't need direct access — deployment is through CI.

## Starting signal

When you're ready to begin, your first deliverable is **M0**: fork the upstream,
get CI green, verify the Docker image builds, and run the regression script
from `handoff/vroom-fork-bench.md` against mainline. Done when the fork's `main`
branch passes the upstream test suite AND produces identical output to upstream
on the fixtures in this briefing.

Good luck.
