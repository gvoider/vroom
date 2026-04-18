#ifndef CO_LOCATION_DEDUP_H
#define CO_LOCATION_DEDUP_H

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

// Busportal fork, M3 / F1. Post-solve pass that collapses maximal runs
// of consecutive pickup steps sharing the same non-empty
// co_located_group on a single route. Within each run:
//
//   - arrival times are set equal to the first member's arrival
//     (travel cost between same-location steps is already 0 in the
//     matrix, so this only rewrites the reported values)
//   - all but one member get service set to 0; the retained step is
//     charged max(service) of the group
//   - route.duration is reduced by the saving
//   - route.cost_breakdown.task is reduced proportionally
//   - summary.computing_times.co_location_savings_seconds accumulates
//     the total saving across all routes
//
// The solver is not modified — it simply observes no travel between
// same-location steps, which is enough to make local search prefer
// consecutive arrangements in practice. Non-consecutive group members
// (rare; only when a different location is interleaved) keep paying
// independent service time, matching RFC §4.1.3's "on the same
// vehicle's route" phrasing.
void apply_co_location_dedup(const Input& input, Solution& sol);

} // namespace utils

} // namespace vroom

#endif
