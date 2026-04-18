# Plan-diff fixtures (Busportal fork, M5 / F4)

Triples of `before-<label>.json` / `after-<label>.json` / `expected-<label>.json`
exercised by `scripts/test-diff.sh`. The fork's `bin/vroom --diff-before A --diff-after B`
reads A and B and emits a diff matching RFC §4.4.2.

## Current fixtures

| Label | Covers |
|---|---|
| `move-and-time` | `moved_vehicle`, `time_changed`, `unchanged`, `unassigned_to_assigned`. Summary `total_unassigned_change=-1`. |

## Adding a fixture

1. Hand-write two minimal solution JSONs (schema: `code`, `summary`, `unassigned`, `routes[].steps[]`). Only the fields the differ actually reads matter — `vehicle`, `steps[].type`, `steps[].id`, `steps[].arrival`, plus `summary.cost` / `distance`, `route.cost` / `distance` / `duration`.
2. Run `bin/vroom --diff-before before-<label>.json --diff-after after-<label>.json > expected-<label>.json`.
3. Verify by eye that every diff type is as expected, then commit all three.
4. `scripts/test-diff.sh` picks up the triple automatically.
