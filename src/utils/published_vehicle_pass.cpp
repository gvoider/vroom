/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include "utils/published_vehicle_pass.h"

#include "structures/typedefs.h"
#include "structures/vroom/input/input.h"

namespace vroom::utils {

namespace {

// Look up the Job attached to a pickup step. Returns nullptr if the
// step isn't a pickup or the lookup fails (shouldn't happen on a well-
// formed solution). We only attribute on pickup steps so a shipment's
// F8 penalty is counted exactly once even though both its pickup and
// delivery Jobs carry the hint.
const Job* pickup_job_for_step(const Input& input, const Step& step) {
  if (step.step_type != STEP_TYPE::JOB || !step.job_type.has_value() ||
      *step.job_type != JOB_TYPE::PICKUP) {
    return nullptr;
  }
  auto it = input.pickup_id_to_rank.find(step.id);
  if (it == input.pickup_id_to_rank.end()) {
    return nullptr;
  }
  return &input.jobs[it->second];
}

} // anonymous namespace

void apply_published_vehicle_pass(const Input& input, Solution& sol) {
  // Short-circuit when no job in the input carries the hint. Cheap
  // no-op on mainline problems.
  bool any_hint = false;
  for (const auto& job : input.jobs) {
    if (job.published_vehicle.has_value()) {
      any_hint = true;
      break;
    }
  }
  if (!any_hint) {
    return;
  }

  for (auto& route : sol.routes) {
    UserCost route_deviation = 0;
    for (const auto& step : route.steps) {
      const Job* job = pickup_job_for_step(input, step);
      if (job == nullptr || !job->published_vehicle.has_value()) {
        continue;
      }
      if (*job->published_vehicle != route.vehicle) {
        route_deviation += job->published_vehicle_cost;
      }
    }
    route.cost_breakdown.published_vehicle_deviation = route_deviation;
    route.cost += route_deviation;
  }

  // Re-accumulate summary totals from routes so every bucket sums to
  // cost (matches the M1 sum invariant guarded by scripts/regression.sh).
  CostBreakdown bd;
  UserCost cost = 0;
  for (const auto& route : sol.routes) {
    bd += route.cost_breakdown;
    cost += route.cost;
  }
  sol.summary.cost_breakdown = bd;
  sol.summary.cost = cost;
}

} // namespace vroom::utils
