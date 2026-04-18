/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <numeric>

#include "utils/co_location_dedup.h"

#include "structures/vroom/input/input.h"
#include "structures/vroom/vehicle.h"

namespace vroom::utils {

namespace {

// Find the index of the vehicle that served this route in `input.vehicles`
// given the route's vehicle id. Returns nullopt if not found (which should
// never happen on a well-formed solution, but we bail out silently rather
// than assert to keep the dedup pass a non-fatal accounting step).
std::optional<std::size_t> find_vehicle_rank(const Input& input, Id vehicle_id) {
  for (std::size_t r = 0; r < input.vehicles.size(); ++r) {
    if (input.vehicles[r].id == vehicle_id) {
      return r;
    }
  }
  return std::nullopt;
}

} // namespace

void apply_co_location_dedup(const Input& input, Solution& sol) {
  UserDuration total_saving = 0;

  for (auto& route : sol.routes) {
    auto v_rank = find_vehicle_rank(input, route.vehicle);
    if (!v_rank.has_value()) {
      continue;
    }
    const auto& v = input.vehicles[*v_rank];

    UserDuration route_saving = 0;

    for (std::size_t i = 0; i < route.steps.size();) {
      auto& anchor = route.steps[i];
      if (anchor.step_type != STEP_TYPE::JOB ||
          anchor.job_type.value_or(JOB_TYPE::SINGLE) != JOB_TYPE::PICKUP ||
          anchor.co_located_group.empty()) {
        ++i;
        continue;
      }

      std::size_t j = i + 1;
      UserDuration max_service = anchor.service;
      UserDuration sum_service = anchor.service;
      while (j < route.steps.size()) {
        const auto& next = route.steps[j];
        if (next.step_type != STEP_TYPE::JOB ||
            next.job_type.value_or(JOB_TYPE::SINGLE) != JOB_TYPE::PICKUP ||
            next.co_located_group != anchor.co_located_group) {
          break;
        }
        max_service = std::max(max_service, next.service);
        sum_service += next.service;
        ++j;
      }

      const std::size_t group_size = j - i;
      if (group_size >= 2 && sum_service > max_service) {
        const UserDuration saving = sum_service - max_service;
        route_saving += saving;

        // Collapse service: keep max on the first step, zero the rest.
        anchor.service = max_service;
        for (std::size_t k = i + 1; k < j; ++k) {
          route.steps[k].service = 0;
        }

        // Equalize arrivals: all group members arrive together. Since
        // travel between same-location steps is zero, the only thing
        // inflating later arrivals is the earlier members' service; we
        // roll those back into a single pooled service applied after
        // the group's last step.
        const UserDuration common_arrival = anchor.arrival;
        for (std::size_t k = i + 1; k < j; ++k) {
          route.steps[k].arrival = common_arrival;
          route.steps[k].waiting_time = 0;
        }

        // Shift every subsequent step's arrival earlier by the saving.
        for (std::size_t k = j; k < route.steps.size(); ++k) {
          if (route.steps[k].arrival >= saving) {
            route.steps[k].arrival -= saving;
          }
        }
      }

      i = j;
    }

    if (route_saving > 0) {
      // `duration` is travel-only in VROOM's model, and travel between
      // same-location steps is already 0 in the matrix. So only the
      // service-time aggregate changes; duration stays put.
      route.service -= route_saving;

      // task-cost component = task_duration × per_task_hour / 3600 in
      // user-cost units. The saving lives in user-duration (seconds),
      // so converting uses the same formula. Guard against the
      // vehicle not being found upstream.
      const auto per_task_hour = v.costs.per_task_hour;
      constexpr double SECONDS_PER_HOUR = 3600.0;
      const UserCost task_cost_saving = static_cast<UserCost>(
        static_cast<double>(route_saving) *
          static_cast<double>(per_task_hour) / SECONDS_PER_HOUR +
        0.5);

      if (route.cost >= task_cost_saving) {
        route.cost -= task_cost_saving;
      }
      if (route.cost_breakdown.task >= task_cost_saving) {
        route.cost_breakdown.task -= task_cost_saving;
      }

      total_saving += route_saving;
    }
  }

  if (total_saving > 0) {
    sol.summary.cost_breakdown.task =
      std::accumulate(sol.routes.begin(),
                      sol.routes.end(),
                      UserCost{0},
                      [](UserCost acc, const Route& r) {
                        return acc + r.cost_breakdown.task;
                      });
    sol.summary.cost = std::accumulate(sol.routes.begin(),
                                       sol.routes.end(),
                                       UserCost{0},
                                       [](UserCost acc, const Route& r) {
                                         return acc + r.cost;
                                       });
    sol.summary.service = std::accumulate(sol.routes.begin(),
                                          sol.routes.end(),
                                          UserDuration{0},
                                          [](UserDuration acc, const Route& r) {
                                            return acc + r.service;
                                          });
  }

  sol.summary.computing_times.co_location_savings_seconds = total_saving;
}

} // namespace vroom::utils
