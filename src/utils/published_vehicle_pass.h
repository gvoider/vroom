#ifndef PUBLISHED_VEHICLE_PASS_H
#define PUBLISHED_VEHICLE_PASS_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include "structures/vroom/solution/solution.h"

namespace vroom {

class Input;

namespace utils {

// Busportal fork, M8 / F8. Post-solve pass that attributes the
// published-vehicle deviation penalty into the `cost_breakdown` bucket.
//
// Per RFC §5.8: when a shipment carries a `published_vehicle` hint and
// is assigned to a different vehicle, charge `published_vehicle_cost`
// once per shipment. De-duplication: a pickup's Job carries the hint
// (its matching delivery Job carries the same hint), but the pass only
// counts on the pickup step so the penalty is charged exactly once.
//
// Scope: this pass ONLY attributes cost post-solve — it does NOT shape
// solver decisions. The solver picks routes using mainline cost; this
// pass exposes "how much this solution deviated from last-published
// assignments" as a tunable bucket, which consumers can tune via
// `published_vehicle_cost` and interpret for UAT. A follow-up milestone
// can wire the penalty into the solver hot path if UAT proves that pure
// post-hoc visibility isn't enough to stabilize re-solve outputs — this
// choice matches M4's accepted "mirror" pattern and stays inside the
// RFC §5.8.5 solve-time budget (no regression, full no-op on mainline
// problems).
//
// No-op when no shipment in the solution carries a published_vehicle
// hint.
void apply_published_vehicle_pass(const Input& input, Solution& sol);

} // namespace utils

} // namespace vroom

#endif
