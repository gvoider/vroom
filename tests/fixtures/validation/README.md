# Validation-error fixtures

Problems that the fork's input validation MUST reject. Not run by
`scripts/regression.sh` (which expects each problem to produce a valid
solution). Exercise them manually with:

```bash
bin/vroom -i tests/fixtures/validation/<fixture>.json
```

and confirm the exit code is 2 with a matching error message.

## Current fixtures

| Fixture | Expected error |
|---|---|
| `problem-soft-tw-reject.json` | `soft_time_window.preferred for job 501 is not contained in any hard time_window.` (Busportal fork, M4 / F2) |
| `problem-published-vehicle-reject.json` | `published_vehicle 99 on job 803 does not match any vehicle id.` (Busportal fork, M8 / F8) |
