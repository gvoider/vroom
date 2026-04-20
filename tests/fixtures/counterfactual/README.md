# Counterfactual fixtures (Busportal fork, M6 / F6)

Input-only fixtures exercised by `scripts/test-counterfactual.sh`. Each
`problem-<label>.json` has the RFC §5.6.2 envelope shape
`{problem, what_if}` with exactly one `what_if` transformation present.
The script asserts shape invariants on the output (see its header
comment); there is no recorded expected file because two solves of
anything non-trivial aren't bit-stable across toolchain noise.

## Current fixtures

| Label | Covers | Expected direction |
|---|---|---|
| `add-vehicles` | `add_vehicles` | `additional_assigned > 0`, `cost_change > 0` |
| `remove-vehicles` | `remove_vehicles` | `additional_assigned < 0`, `cost_change < 0` |
| `relax-tw` | `relax_time_windows` | `additional_assigned > 0`, `cost_change > 0` |
| `add-shipments` | `add_shipments` | `cost_change > 0` |
| `remove-shipments` | `remove_shipments` | `cost_change < 0` |

## First-one-wins order (RFC §5.6.2)

```
add_vehicles > remove_vehicles > relax_time_windows > add_shipments > remove_shipments
```

When the fixture declares multiple `what_if` keys, the implementation
applies only the first one in that order — the rest are silently
ignored and the `improvement.applied_what_if` field reports which key
won.
