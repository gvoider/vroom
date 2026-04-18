/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>

#include "utils/soft_time_window_pass.h"

#include "structures/typedefs.h"
#include "structures/vroom/input/input.h"
#include "structures/vroom/vehicle.h"

namespace vroom::utils {

namespace {

constexpr UserDuration INF_USER_DURATION =
  std::numeric_limits<UserDuration>::max() / 2;

// Find the vehicle's rank in `input.vehicles` by its id. Returns nullopt
// on mismatch (shouldn't happen on a well-formed solution).
std::optional<std::size_t> find_vehicle_rank(const Input& input,
                                             Id vehicle_id) {
  for (std::size_t r = 0; r < input.vehicles.size(); ++r) {
    if (input.vehicles[r].id == vehicle_id) {
      return r;
    }
  }
  return std::nullopt;
}

// Look up the step's latest feasible arrival allowed by its own hard
// time windows. For start/end steps we return INF (vehicle's own TW is
// handled separately below). For job steps we pick the hard TW whose
// [start,end] contains the solver's chosen arrival; that TW caps our
// delay. No hard TW on the job ⇒ INF.
UserDuration step_hard_tw_end(const Input& input, const Step& step) {
  if (step.step_type != STEP_TYPE::JOB || !step.job_type.has_value()) {
    return INF_USER_DURATION;
  }
  // Resolve the Job via id + job_type. Pickups and deliveries share the
  // shipment id, so we MUST pick the map that matches the step's type;
  // otherwise the delivery step would look up the pickup Job (or
  // vice-versa) and inherit the wrong TW.
  const Job* job = nullptr;
  switch (*step.job_type) {
  case JOB_TYPE::SINGLE:
    if (auto it = input.job_id_to_rank.find(step.id);
        it != input.job_id_to_rank.end()) {
      job = &input.jobs[it->second];
    }
    break;
  case JOB_TYPE::PICKUP:
    if (auto it = input.pickup_id_to_rank.find(step.id);
        it != input.pickup_id_to_rank.end()) {
      job = &input.jobs[it->second];
    }
    break;
  case JOB_TYPE::DELIVERY:
    if (auto it = input.delivery_id_to_rank.find(step.id);
        it != input.delivery_id_to_rank.end()) {
      job = &input.jobs[it->second];
    }
    break;
  }
  if (job == nullptr || job->tws.empty()) {
    return INF_USER_DURATION;
  }
  const Duration user_arrival =
    static_cast<Duration>(step.arrival) * DURATION_FACTOR;
  for (const auto& tw : job->tws) {
    if (tw.is_default()) {
      return INF_USER_DURATION;
    }
    if (tw.start <= user_arrival && user_arrival <= tw.end) {
      return static_cast<UserDuration>(tw.end / DURATION_FACTOR);
    }
  }
  // Arrival already outside every hard TW (VROOM shouldn't produce
  // this, but be safe — treat as no slack).
  return step.arrival;
}

// Travel duration (user-seconds) between two consecutive steps' indexed
// locations as seen by the vehicle's cost wrapper. When either step has
// no location (shouldn't happen for non-break steps) we return 0.
UserDuration travel_between(const Vehicle& v, const Step& a, const Step& b) {
  if (!a.location.has_value() || !b.location.has_value()) {
    return 0;
  }
  const auto raw =
    v.cost_wrapper.duration(a.location->index(), b.location->index());
  return static_cast<UserDuration>(raw / DURATION_FACTOR);
}

// The target arrival that minimizes soft cost for this step, bounded by
// [earliest_ok, latest_ok]. We only ever shift later, never earlier, so
// `earliest_ok` is the step's current arrival and `latest_ok` is the
// backward-computed latest-feasible.
//
// Decision table:
//   arrival < preferred_start, preferred_start <= latest_ok
//       → shift to min(latest_ok, preferred_end) (inside preferred when possible)
//   arrival < preferred_start, preferred_start > latest_ok
//       → shift to latest_ok (best we can do; still inside hard TW)
//   arrival inside preferred
//       → shift to min(latest_ok, preferred_end) if cost_after <= cost_before
//         (leaning late inside preferred unless after-cost is punitive)
//   arrival > preferred_end
//       → stay put; we don't reach back with this pass
UserDuration soft_tw_target(const SoftTimeWindow& soft,
                            UserDuration arrival,
                            UserDuration latest_ok) {
  if (!soft.present) {
    return arrival;
  }
  const auto user_pref_end =
    static_cast<UserDuration>(soft.preferred_end / DURATION_FACTOR);

  if (arrival >= user_pref_end) {
    // Already at or past the preferred end; pulling earlier would
    // require a backward shift we don't do here. Keep as-is.
    return arrival;
  }

  // Otherwise consider shifting later. Bounded by latest_ok.
  if (latest_ok <= arrival) {
    return arrival;
  }

  // If reaching `user_pref_start` is feasible, hop to the latest inside
  // preferred (tying up against cost_per_second_after only when
  // `latest_ok` forces us past). Else hop to latest_ok.
  UserDuration target = latest_ok;
  if (user_pref_end < latest_ok) {
    target = user_pref_end;
  }
  if (target < arrival) {
    return arrival;
  }
  return target;
}

} // anonymous namespace

void apply_soft_time_window_pass(const Input& input, Solution& sol) {
  // Short-circuit when the solution doesn't carry any soft TW at all.
  // Cheap no-op for mainline problems.
  bool any_soft = false;
  for (const auto& route : sol.routes) {
    for (const auto& step : route.steps) {
      if (step.soft_time_window.present) {
        any_soft = true;
        break;
      }
    }
    if (any_soft) {
      break;
    }
  }
  if (!any_soft) {
    return;
  }

  for (auto& route : sol.routes) {
    auto v_rank = find_vehicle_rank(input, route.vehicle);
    if (!v_rank.has_value() || route.steps.empty()) {
      continue;
    }
    const auto& v = input.vehicles[*v_rank];
    const auto n = route.steps.size();

    // Per-step latest-feasible arrival: backward pass.
    std::vector<UserDuration> latest(n, INF_USER_DURATION);
    const UserDuration veh_tw_end =
      v.tw.is_default()
        ? INF_USER_DURATION
        : static_cast<UserDuration>(v.tw.end / DURATION_FACTOR);
    // End step's latest is bounded by the vehicle's own TW end.
    latest[n - 1] = std::min(step_hard_tw_end(input, route.steps[n - 1]),
                             veh_tw_end);
    for (std::size_t k = n - 1; k > 0; --k) {
      const auto travel = travel_between(v, route.steps[k - 1], route.steps[k]);
      UserDuration upstream_cap = INF_USER_DURATION;
      if (latest[k] >
          route.steps[k - 1].service + route.steps[k - 1].setup + travel) {
        upstream_cap = latest[k] - route.steps[k - 1].service -
                       route.steps[k - 1].setup - travel;
      } else {
        upstream_cap = route.steps[k - 1].arrival; // no slack
      }
      latest[k - 1] =
        std::min(step_hard_tw_end(input, route.steps[k - 1]), upstream_cap);
      // Can't go earlier than the step's actual solver-chosen arrival.
      if (latest[k - 1] < route.steps[k - 1].arrival) {
        latest[k - 1] = route.steps[k - 1].arrival;
      }
    }

    // Forward pass: decide a delay for each soft-TW step and cascade.
    // The delay increment at step k is physically "the vehicle waited
    // longer before departing the previous location", so we charge it
    // to step[k-1].waiting_time. That matches VROOM's invariant
    //     step[k].arrival == step[k-1].arrival + step[k-1].setup
    //                      + step[k-1].service + step[k-1].waiting_time
    //                      + travel(k-1, k)
    // and keeps the step-timing invariant intact (waiting at step k-1
    // is in the cumulative-non-travel<k sum the invariant counts).
    UserDuration delay = 0;
    UserDuration route_soft_delay = 0;
    for (std::size_t k = 0; k < n; ++k) {
      auto& step = route.steps[k];
      const UserDuration new_arrival = step.arrival + delay;
      const UserDuration step_latest = latest[k];

      UserDuration increment = 0;
      if (step.soft_time_window.present && new_arrival < step_latest) {
        const auto target = soft_tw_target(step.soft_time_window,
                                           new_arrival,
                                           step_latest);
        if (target > new_arrival) {
          increment = target - new_arrival;
        }
      }
      if (increment > 0) {
        // Attribute the delay to the preceding step's waiting_time.
        // The very first step has no predecessor; put the wait on
        // itself (the vehicle delays its own departure from start).
        if (k == 0) {
          step.waiting_time += increment;
        } else {
          route.steps[k - 1].waiting_time += increment;
        }
        delay += increment;
        route_soft_delay += increment;
      }
      step.arrival = step.arrival + delay;

      if (step.soft_time_window.present) {
        step.soft_window_violation_cost =
          step.soft_time_window.violation_cost(step.arrival);
      } else {
        step.soft_window_violation_cost = 0;
      }
    }

    // Route-level waiting_time reflects the sum of step waiting_times,
    // so we bump it by the total delay introduced here.
    route.waiting_time += route_soft_delay;

    // Aggregate route-level soft cost and fold into breakdown + cost.
    const UserCost route_soft_cost = std::accumulate(
      route.steps.begin(),
      route.steps.end(),
      UserCost{0},
      [](UserCost acc, const Step& s) {
        return acc + s.soft_window_violation_cost;
      });
    if (route_soft_cost > 0) {
      route.cost_breakdown.soft_time_window_violation = route_soft_cost;
      route.cost += route_soft_cost;
    } else {
      route.cost_breakdown.soft_time_window_violation = 0;
    }

    // Route-level `duration` is cumulative travel; unchanged by soft
    // shifts (travel is the same, only arrival timestamps move). Same
    // for `service` / `setup` / `waiting_time` aggregates.
  }

  // Re-accumulate summary totals from routes, matching the M3.1 helper
  // spirit: keep sum(breakdown) == cost across every bucket so consumers
  // don't have to patch together separate sources of truth.
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
