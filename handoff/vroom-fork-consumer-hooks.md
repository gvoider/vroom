# VROOM Fork — Consumer Integration Hooks

Exact locations in `backend-dispatch` where each milestone's consumer-side change lands. Open these files, find the cited line ranges, and make the change described.

Line numbers reference `backend-dispatch` at commit `de80224` on branch `dev` (2026-04-18). If line numbers drift, grep for the landmark text in each row.

---

## M0 — no consumer change (fork scaffolding only)

## M1 — Cost breakdown (F3)

### Add parsing to `VroomClient::solve`

- **File**: `backend-dispatch/src/Dispatch/VroomClient.php`
- **Symbol**: `VroomClient::solve`
- **Lines**: 627–692
- **Landmark**: `$solution = json_decode($resp, true);` near line 660
- **Change**: after the existing response decode, extract `summary.cost_breakdown` into a new field on the returned array. No behavioral change to solve flow.

### Expose to the dispatcher UI

- **File**: `backend-dispatch/src/Dispatch/DispatchController.php`
- **Symbol**: `DispatchController::optimizeDay` — the big response-assembly block around lines 1190–1260
- **Add**: `'costBreakdown' => $solution['summary']['cost_breakdown'] ?? null`
- **Consumer side in frontend (admin-dispatch)**: new UI panel outside scope of this handoff — just surface the JSON.

### Acceptance
- `curl -X POST http://vroom-fork:3000/ -d @problem-shipments-3.json | jq .summary.cost_breakdown` returns a populated object.
- `optimizeDay` response contains `costBreakdown`.

---

## M2 — Richer unassigned diagnostics (F5)

### Simplify `DispatchDiagnostician::classifyOne`

- **File**: `backend-dispatch/src/Dispatch/DispatchDiagnostician.php`
- **Symbol**: `DispatchDiagnostician::classifyOne`
- **Lines**: 102–180
- **Current LoC**: ~80
- **Target LoC**: ~30
- **What to delete**:
  - The `extractPeeledRequestIds` arg plumbing (the richer reason from VROOM obviates our peel-matching heuristic).
  - The `isWindowTight` local check (VROOM reason `time_window_infeasible` is more accurate).
  - The `$capacityShortfall` / `sumPassengers` / `sumFleetCapacity` fleet-capacity pre-computation — VROOM now tells us per-shipment.
- **What becomes**:
  ```php
  private function classifyOne(int $requestId, $req, array $unassignedEntry): array {
      $reason = $unassignedEntry['reason'] ?? 'unknown';
      $details = $unassignedEntry['details'] ?? [];

      return match($reason) {
          'no_vehicle_with_required_skills' => $this->makeDiagnostic(
              $requestId, self::REASON_NO_VEHICLE, self::HINT_CONTACT_ADMIN, $details),
          'time_window_infeasible' => $this->makeDiagnostic(
              $requestId, self::REASON_TIME_WINDOW_TIGHT, self::HINT_CHANGE_TIME, $details),
          'capacity_exceeded' => $this->makeDiagnostic(
              $requestId, self::REASON_CAPACITY_EXCEEDED, self::HINT_ADD_ROUTE, $details),
          default => $this->makeDiagnostic(
              $requestId, self::REASON_UNKNOWN, self::HINT_REVIEW_CONSTRAINTS, $details),
      };
  }
  ```

### Update caller `diagnose()`

- **Symbol**: `DispatchDiagnostician::diagnose`
- **Lines**: 41–100
- **Change**: pass the full unassigned entry (from VROOM) into `classifyOne` instead of the pre-computed context args.

### Opt-in via query param

- **File**: `VroomClient.php`
- **Symbol**: `VroomClient::solve`
- **Change**: append `?diagnostics=full` to the URL so mainline hot-path solves don't pay the extra cost. This only applies if the fork gates diagnostics behind the param (RFC §4.5.2).

---

## M3 — Native co-located shared-stop (F1)

**This is the largest consumer delta: ~540 LoC deleted.**

### Delete `DispatchSharedPickupBatcher` entirely

- **File**: `backend-dispatch/src/Dispatch/DispatchSharedPickupBatcher.php`
- **Lines**: 1–466 (full file)
- **Action**: `git rm` after the following updates.

### Simplify `VroomClient::solveIteratively`

- **Lines**: 92–173
- **Current LoC**: ~80
- **Target LoC**: ~25
- **What to delete**: the entire outer iteration loop + peel-off logic (lines 109–163). The new body is just:
  ```php
  $contexts = $this->buildRequestContexts($requests, $stopTimezone, $serviceTimeSeconds, $locationOverrides);
  $problem = $this->assembleProblemWithoutBatching($contexts, $vroomVehicles, $stationLngLat, $tentativeConfirmId);
  $solution = $this->solve($problem);
  $solution = $this->shiftRoutesLate($solution, $shipmentWindows);  // removed at M4
  return [
      'solution' => $solution,
      'shipmentWindows' => $shipmentWindows,
      'shipmentRequestIds' => $simpleIdentityMap,
      'iterations' => 1,
      'peelLog' => [],
  ];
  ```

### Replace `assembleProblem` → `assembleProblemWithoutBatching`

- **Lines**: 248–348
- **Current**: handles synthetic shipment IDs, shared-stop merge, `shipmentIdToBatchLeader` map
- **New**: each request becomes one shipment; add `co_located_group` field derived from `sharedStopKey`:
  ```php
  $shipments[] = [
      'pickup' => [
          'id' => $shipmentId,
          'location' => [(float) $context['pickupLng'], (float) $context['pickupLat']],
          'service' => (int) $context['pickupServiceTime'],  // can now be non-zero
          'description' => (string) $context['pickupDescription'],
          'time_windows' => [[(int) $context['pickupEarliest'], (int) $context['pickupLatest']]],
          'co_located_group' => $context['sharedStopKey'],    // NEW, was hidden in batcher
      ],
      'delivery' => [ ... unchanged ... ],
      'amount' => [(int) $context['passengerCount'], (int) $context['luggageCount']],
      'priority' => $this->computeContextPriority($context, $tentativeConfirmId),
  ];
  ```
- **Delete**: `$batchedRequestIds`, `$batchLeaders`, synthetic-ID logic, `computeBatchPriority`, `shipmentIdToBatchLeader` map, `shipmentRequestIds` (becomes identity).

### Update extractors that consume the old batching maps

- `extractCallOrder` (lines 701–725): drop `$shipmentRequestIds` expansion; a single step = a single request.
- `extractPickupTimes` (lines 755–776): same.
- `extractRoutePickups` (lines 785–814): same.
- `expandShipmentRequestIds` helper: DELETE entirely.

### Acceptance
- Solve with 3 passengers at the same coords: all three arrive at the same `arrival` time on the same vehicle, service time charged once.
- Run the full dispatcher-day flow on a real day from our dev DB; no request shuffles unexpectedly, no new unassigned cases.

---

## M4 — Soft time windows (F2)

### Delete `shiftRoutesLate`

- **File**: `VroomClient.php`
- **Symbol**: `VroomClient::shiftRoutesLate`
- **Lines**: 564–618
- **Action**: delete the method and remove its call site in `solveIteratively`.

### Emit `soft_time_window` for pending pickups

- **Symbol**: `VroomClient::buildRequestContext`
- **Lines**: 391–452
- **Change**: when building the pickup step, add `soft_time_window` when the request is pending (not confirmed):
  ```php
  // Only pending pickups get soft windows; confirmed stay hard-tight.
  if (!$isConfirmed) {
      $softPreferred = [max(0, $deliveryLatest - 1800), $deliveryLatest]; // 30 min before cutoff to cutoff
      $context['softTimeWindow'] = [
          'preferred' => $softPreferred,
          'cost_per_second_before' => 0.1,
          'cost_per_second_after' => 100.0,
      ];
  }
  ```
- Plumb through `assembleProblemWithoutBatching` to the JSON.

### Acceptance
- A day's solve with mixed confirmed + pending routes no longer needs post-processing; arrivals land close to trip departure naturally.

---

## M5 — Plan diff endpoint (F4)

### New consumer class

- **New file**: `backend-dispatch/src/Dispatch/DispatchPlanDiffer.php`
- **Expected LoC**: ~50
- **Purpose**: wrap a call to `POST /diff` and expose to `DispatchController`.

### Wire into `optimizeDay`

- **File**: `DispatchController.php`
- **Symbol**: `DispatchController::optimizeDay`
- **Change**: if the request carries a `previousSolution` field (UI persists last solution), call `/diff` after the new solve and return the diff.

---

## M6 — Counterfactual (F6)

### New endpoint

- **File**: `DispatchController.php`
- **Symbol**: NEW `DispatchController::whatIf`
- **Pattern**: mirror `optimizeDay` but call `POST /counterfactual` instead of `/`.

---

## M7 — Driver shifts and breaks (F7)

### Extend `buildVroomVehicles`

- **File**: `VroomClient.php`
- **Symbol**: `VroomClient::buildVroomVehicles`
- **Lines**: 182–198
- **Change**: if the vehicle record has `shifts` or `breaks` fields (from a `driver_schedules` table — new), emit them per-vehicle.

---

## M8 — Published-vehicle soft penalty (F8)

### Emit `published_vehicle` per shipment

- **File**: `VroomClient.php`
- **Symbol**: `VroomClient::assembleProblemWithoutBatching` (post M3)
- **Change**: when a request has `publishedVehicleId` (from prior solve, stored on DispatchRequest), emit:
  ```php
  if (!empty($context['publishedVehicleId'])) {
      $shipment['published_vehicle'] = (int) $context['publishedVehicleId'];
      $shipment['published_vehicle_cost'] = 500;
  }
  ```

---

## Test strategy — HONEST current state

**`backend-dispatch` has no test suite**. No PHPUnit, no Pest. `composer.json` scripts:
```json
"cs-fix": "php-cs-fixer fix $1",
"analyse": "phpstan analyse --memory-limit 300M"
```

That's it. No test command.

### Implication for milestone acceptance

The RFC's "consumer tests pass" criterion is currently **unverifiable** — there are none to pass. Two realistic options:

**Option A — write a minimal fixture-based integration test per milestone**.
- Each milestone PR adds a PHP fixture under `backend-dispatch/tests/Dispatch/`.
- Fixture: JSON problem file + expected assertions on the solve response.
- Entry point: `php bin/hyperf.php dispatch:test-solve <fixture-file>` — a new Hyperf command.
- Run manually at milestone UAT; add to a future CI stage.

**Option B — skip formal tests; rely on dev UAT only**.
- Each milestone: deploy fork to dev, run real dispatch day, inspect output, get dispatcher sign-off.
- Riskier but matches the project's existing QA style.

Recommendation: **Option A for M1/M2/M3 (low-risk features), Option B is acceptable for M4–M8 if dispatcher UAT passes**.

### phpstan

`composer analyse` is the only automated gate. Ensure the consumer changes each milestone leave phpstan clean.

---

## Rollback per milestone

Because we deploy the fork on Path B (alongside existing VROOM; see `vroom-fork-env.md §5`), each milestone rollback is a one-command env flip:

```bash
kubectl --context dev -n bus-vdexpress set env deployment/backend-dispatch \
  VROOM_SERVICE_URL=http://vroom:3000/     # revert to mainline pod
```

The consumer PR for each milestone MUST:
1. Default the new feature off unless a new env var or feature flag is set, OR
2. Be additive (new fields in input/output, no behavioral change when absent).

This keeps both versions deployable and reversible at all times.
