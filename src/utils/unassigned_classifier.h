#ifndef UNASSIGNED_CLASSIFIER_H
#define UNASSIGNED_CLASSIFIER_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <vector>

#include "structures/vroom/job.h"
#include "structures/vroom/solution/unassigned_info.h"

namespace vroom {

class Input;

namespace utils {

// Classify each unassigned job into one of the reasons listed in
// UnassignedReason. Order of checks mirrors RFC §4.5 (F5) so the reason
// that "fires" is the most restrictive one that applies in isolation:
//
//   1. no_vehicle_with_required_skills
//   2. capacity_exceeded
//   3. time_window_infeasible
//   4. max_travel_time_exceeded
//   5. route_duration_limit_exceeded
//   6. no_feasible_insertion  (fallback when the job looks admissible
//                              for at least one vehicle but the global
//                              solver still couldn't place it)
//
// The returned vector is parallel to `unassigned_jobs`. Cost-wise this is
// O(|unassigned| × |vehicles|) matrix lookups — trivially fast for our
// problem sizes, but the whole pass only runs when the caller requests it.
std::vector<UnassignedInfo>
classify_unassigned(const Input& input,
                    const std::vector<Job>& unassigned_jobs);

} // namespace utils

} // namespace vroom

#endif
