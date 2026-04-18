#ifndef SOFT_TIME_WINDOW_H
#define SOFT_TIME_WINDOW_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include "structures/typedefs.h"

namespace vroom {

// Busportal fork, M4 / F2. Optional soft-time-window hint on a step.
// When the step's arrival lands outside [preferred_start, preferred_end]
// the solver pays a linear penalty per second of deviation — see RFC §4.2.
// When absent, the step behaves exactly like mainline.
//
// `present` is the cheap presence flag so the rest of the codebase can
// branch on `soft.present` without having to wrap this in std::optional
// at every field. Construction sets it to true; the default-constructed
// struct (present=false) is a no-op on every consumer.
struct SoftTimeWindow {
  bool present{false};
  Duration preferred_start{0};
  Duration preferred_end{0};
  double cost_per_second_before{0.0};
  double cost_per_second_after{0.0};

  SoftTimeWindow() = default;

  SoftTimeWindow(UserDuration preferred_start_user,
                 UserDuration preferred_end_user,
                 double cost_per_second_before,
                 double cost_per_second_after);

  // Return the soft-cost penalty (in the fork's integer cost unit) for
  // an arrival expressed in user-seconds. Zero when the step is inside
  // the preferred interval or when soft is absent.
  UserCost violation_cost(UserDuration arrival_user) const;
};

} // namespace vroom

#endif
