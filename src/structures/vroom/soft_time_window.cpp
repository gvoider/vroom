/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include "structures/vroom/soft_time_window.h"

#include "utils/helpers.h"

namespace vroom {

SoftTimeWindow::SoftTimeWindow(UserDuration preferred_start_user,
                               UserDuration preferred_end_user,
                               double cost_per_second_before,
                               double cost_per_second_after)
  : present(true),
    preferred_start(utils::scale_from_user_duration(preferred_start_user)),
    preferred_end(utils::scale_from_user_duration(preferred_end_user)),
    cost_per_second_before(cost_per_second_before),
    cost_per_second_after(cost_per_second_after) {
}

UserCost SoftTimeWindow::violation_cost(UserDuration arrival_user) const {
  if (!present) {
    return 0;
  }
  const auto user_pref_start =
    static_cast<UserDuration>(preferred_start / DURATION_FACTOR);
  const auto user_pref_end =
    static_cast<UserDuration>(preferred_end / DURATION_FACTOR);

  if (arrival_user < user_pref_start) {
    return utils::round<UserCost>(cost_per_second_before *
                                  static_cast<double>(user_pref_start -
                                                      arrival_user));
  }
  if (arrival_user > user_pref_end) {
    return utils::round<UserCost>(cost_per_second_after *
                                  static_cast<double>(arrival_user -
                                                      user_pref_end));
  }
  return 0;
}

} // namespace vroom
