#ifndef SOFT_TIME_WINDOW_PASS_H
#define SOFT_TIME_WINDOW_PASS_H

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

// Busportal fork, M4 / F2. Post-solve pass that:
//
//   1. Backward-computes the latest-feasible arrival per step per route
//      (bounded by each step's own hard TW and vehicle.tw.end).
//   2. Walks forward and, for each step carrying a soft_time_window,
//      greedily delays its arrival as far as the latest-feasible
//      allows — targeting the preferred interval when reachable. Every
//      subsequent step's arrival shifts by the same amount, so the
//      pass propagates forward with a running "delay accumulator".
//   3. Computes per-step `soft_window_violation_cost` at the final
//      arrivals and aggregates into `route.cost_breakdown.
//      soft_time_window_violation`, `route.cost`, and the summary.
//
// Replaces the consumer's `VroomClient::shiftRoutesLate` (~55 LoC PHP).
// When no step in the solution carries a soft_time_window the pass is
// a full no-op.
void apply_soft_time_window_pass(const Input& input, Solution& sol);

} // namespace utils

} // namespace vroom

#endif
