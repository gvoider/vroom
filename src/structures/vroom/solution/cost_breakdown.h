#ifndef COST_BREAKDOWN_H
#define COST_BREAKDOWN_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include "structures/typedefs.h"

namespace vroom {

// Additive per-objective breakdown of a Route's or Summary's total cost.
// The invariant is: fixed_vehicle + duration + distance + task + priority_bias
// + soft_time_window_violation + published_vehicle_deviation == route.cost
// within integer rounding (|drift| <= 1 unit per route; aggregates sum
// linearly so the drift on the summary is bounded by route count).
//
// Two of the forward-looking fields are now populated: soft_time_window_violation
// (M4 — `utils::apply_soft_time_window_pass`) and published_vehicle_deviation
// (M8 — `utils::apply_published_vehicle_pass`). `priority_bias` is still
// reserved for a future milestone and stays at zero; the invariant holds on
// mainline-compatible problems because both populated passes are no-ops when
// no shipment / step carries the corresponding hint.
struct CostBreakdown {
  UserCost fixed_vehicle{0};
  UserCost duration{0};
  UserCost distance{0};
  UserCost task{0};
  UserCost priority_bias{0};
  UserCost soft_time_window_violation{0};
  UserCost published_vehicle_deviation{0};

  UserCost total() const {
    return fixed_vehicle + duration + distance + task + priority_bias +
           soft_time_window_violation + published_vehicle_deviation;
  }

  CostBreakdown& operator+=(const CostBreakdown& rhs) {
    fixed_vehicle += rhs.fixed_vehicle;
    duration += rhs.duration;
    distance += rhs.distance;
    task += rhs.task;
    priority_bias += rhs.priority_bias;
    soft_time_window_violation += rhs.soft_time_window_violation;
    published_vehicle_deviation += rhs.published_vehicle_deviation;
    return *this;
  }

  friend CostBreakdown operator+(CostBreakdown lhs, const CostBreakdown& rhs) {
    lhs += rhs;
    return lhs;
  }
};

} // namespace vroom

#endif
