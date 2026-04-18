/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <limits>

#include "utils/unassigned_classifier.h"

#include "structures/typedefs.h"
#include "structures/vroom/input/input.h"
#include "structures/vroom/vehicle.h"

namespace vroom::utils {

namespace {

bool vehicle_has_all_skills(const Vehicle& v, const Skills& required) {
  return std::ranges::all_of(required, [&](Skill s) {
    return v.skills.contains(s);
  });
}

// Returns the index of the first capacity dimension the vehicle cannot
// carry for this job, or nullopt if the vehicle's capacity is OK.
std::optional<unsigned> capacity_shortfall(const Vehicle& v, const Job& j) {
  const auto& pickup = j.pickup;
  const auto& delivery = j.delivery;
  for (unsigned d = 0; d < v.capacity.size(); ++d) {
    if (v.capacity[d] < pickup[d] || v.capacity[d] < delivery[d]) {
      return d;
    }
  }
  return std::nullopt;
}

// Earliest the vehicle could arrive at the job's location, starting from
// its own start location at the start of its time window. Returns nullopt
// if the vehicle cannot compute a duration (no start/end and no matrix).
std::optional<UserDuration> earliest_arrival(const Vehicle& v, const Job& j) {
  UserDuration user_start = static_cast<UserDuration>(v.tw.start / DURATION_FACTOR);
  if (v.has_start()) {
    const auto leg = v.cost_wrapper.duration(v.start.value().index(), j.index());
    return user_start + static_cast<UserDuration>(leg / DURATION_FACTOR);
  }
  return user_start;
}

// Best round-trip duration (start → job → end) the vehicle would incur.
UserDuration round_trip_duration(const Vehicle& v, const Job& j) {
  Duration total = 0;
  if (v.has_start()) {
    total += v.cost_wrapper.duration(v.start.value().index(), j.index());
  }
  if (v.has_end()) {
    total += v.cost_wrapper.duration(j.index(), v.end.value().index());
  }
  return static_cast<UserDuration>(total / DURATION_FACTOR);
}

// True iff any of the job's time windows can contain the vehicle's
// arrival time.
bool arrival_fits_any_window(const std::vector<TimeWindow>& tws,
                             UserDuration arrival) {
  const Duration scaled = static_cast<Duration>(arrival) * DURATION_FACTOR;
  return std::ranges::any_of(tws, [&](const TimeWindow& tw) {
    return scaled <= tw.end && scaled + 0 >= tw.start;
  });
}

} // anonymous namespace

std::vector<UnassignedInfo>
classify_unassigned(const Input& input,
                    const std::vector<Job>& unassigned_jobs) {
  std::vector<UnassignedInfo> out;
  out.reserve(unassigned_jobs.size());

  const auto& vehicles = input.vehicles;

  for (const auto& j : unassigned_jobs) {
    UnassignedInfo info;

    // 1. Skills: which vehicles have all required skills?
    std::vector<Id> vehicles_missing_skills;
    std::vector<Index> skill_compatible_ranks;
    for (std::size_t v_rank = 0; v_rank < vehicles.size(); ++v_rank) {
      if (vehicle_has_all_skills(vehicles[v_rank], j.skills)) {
        skill_compatible_ranks.push_back(static_cast<Index>(v_rank));
      } else {
        vehicles_missing_skills.push_back(vehicles[v_rank].id);
      }
    }
    if (skill_compatible_ranks.empty()) {
      info.reason = UnassignedReason::no_vehicle_with_required_skills;
      info.details.required_skills =
        std::vector<Skill>(j.skills.begin(), j.skills.end());
      info.details.vehicles_missing_skills = std::move(vehicles_missing_skills);
      out.push_back(std::move(info));
      continue;
    }

    // 2. Capacity: any skill-compatible vehicle big enough on every dimension?
    std::vector<Index> capacity_compatible_ranks;
    std::optional<unsigned> failing_dim;
    Capacity max_available_on_failing_dim = 0;
    Capacity required_on_failing_dim = 0;
    for (auto v_rank : skill_compatible_ranks) {
      if (auto fail = capacity_shortfall(vehicles[v_rank], j); !fail) {
        capacity_compatible_ranks.push_back(v_rank);
      } else if (!failing_dim.has_value()) {
        failing_dim = fail;
        max_available_on_failing_dim = vehicles[v_rank].capacity[*fail];
        required_on_failing_dim =
          std::max(j.pickup[*fail], j.delivery[*fail]);
      } else {
        max_available_on_failing_dim =
          std::max(max_available_on_failing_dim,
                   vehicles[v_rank].capacity[*failing_dim]);
      }
    }
    if (capacity_compatible_ranks.empty()) {
      info.reason = UnassignedReason::capacity_exceeded;
      info.details.capacity_dimension = failing_dim;
      info.details.required_capacity = required_on_failing_dim;
      info.details.max_available_capacity = max_available_on_failing_dim;
      out.push_back(std::move(info));
      continue;
    }

    // 3. Time windows: any candidate vehicle whose earliest arrival fits
    //    a job window?
    if (!j.tws.empty() && !j.tws.front().is_default()) {
      std::optional<Id> closest_vehicle;
      std::optional<UserDuration> closest_arrival;
      UserDuration smallest_shortfall =
        std::numeric_limits<UserDuration>::max();
      bool any_fits = false;
      for (auto v_rank : capacity_compatible_ranks) {
        const auto& v = vehicles[v_rank];
        auto arr = earliest_arrival(v, j);
        if (!arr.has_value()) {
          continue;
        }
        if (arrival_fits_any_window(j.tws, *arr)) {
          any_fits = true;
          break;
        }
        // Shortfall to the closest window (either late or early).
        UserDuration local_shortfall =
          std::numeric_limits<UserDuration>::max();
        for (const auto& tw : j.tws) {
          UserDuration user_tw_start =
            static_cast<UserDuration>(tw.start / DURATION_FACTOR);
          UserDuration user_tw_end =
            static_cast<UserDuration>(tw.end / DURATION_FACTOR);
          UserDuration delta = (*arr > user_tw_end)
                                 ? *arr - user_tw_end
                                 : (user_tw_start > *arr)
                                     ? user_tw_start - *arr
                                     : 0;
          local_shortfall = std::min(local_shortfall, delta);
        }
        if (local_shortfall < smallest_shortfall) {
          smallest_shortfall = local_shortfall;
          closest_vehicle = v.id;
          closest_arrival = *arr;
        }
      }
      if (!any_fits) {
        info.reason = UnassignedReason::time_window_infeasible;
        info.details.earliest = static_cast<UserDuration>(
          j.tws.front().start / DURATION_FACTOR);
        info.details.latest = static_cast<UserDuration>(
          j.tws.front().end / DURATION_FACTOR);
        info.details.closest_feasible_vehicle = closest_vehicle;
        info.details.closest_feasible_arrival = closest_arrival;
        if (smallest_shortfall != std::numeric_limits<UserDuration>::max()) {
          info.details.shortfall_seconds = smallest_shortfall;
        }
        out.push_back(std::move(info));
        continue;
      }
    }

    // 4. max_travel_time: does every candidate vehicle's round trip bust
    //    its own limit?
    {
      bool any_within = false;
      UserDuration smallest_would_require =
        std::numeric_limits<UserDuration>::max();
      UserDuration matching_max = 0;
      for (auto v_rank : capacity_compatible_ranks) {
        const auto& v = vehicles[v_rank];
        const auto rt = round_trip_duration(v, j);
        const auto user_max_tt =
          static_cast<UserDuration>(v.max_travel_time / DURATION_FACTOR);
        if (v.max_travel_time == std::numeric_limits<Duration>::max() ||
            rt <= user_max_tt) {
          any_within = true;
          break;
        }
        if (rt < smallest_would_require) {
          smallest_would_require = rt;
          matching_max = user_max_tt;
        }
      }
      if (!any_within) {
        info.reason = UnassignedReason::max_travel_time_exceeded;
        info.details.max_allowed_seconds = matching_max;
        info.details.would_require_seconds = smallest_would_require;
        out.push_back(std::move(info));
        continue;
      }
    }

    // 5. Route duration limit (vehicle time window).
    {
      bool any_fits_tw = false;
      UserDuration smallest_would_require =
        std::numeric_limits<UserDuration>::max();
      UserDuration matching_max = 0;
      for (auto v_rank : capacity_compatible_ranks) {
        const auto& v = vehicles[v_rank];
        if (v.tw.is_default()) {
          any_fits_tw = true;
          break;
        }
        const auto rt = round_trip_duration(v, j);
        const auto vehicle_tw_length =
          static_cast<UserDuration>(v.tw.length / DURATION_FACTOR);
        const auto service_user =
          static_cast<UserDuration>(j.default_service);
        if (rt + service_user <= vehicle_tw_length) {
          any_fits_tw = true;
          break;
        }
        if (rt + service_user < smallest_would_require) {
          smallest_would_require = rt + service_user;
          matching_max = vehicle_tw_length;
        }
      }
      if (!any_fits_tw) {
        info.reason = UnassignedReason::route_duration_limit_exceeded;
        info.details.max_allowed_seconds = matching_max;
        info.details.would_require_seconds = smallest_would_require;
        out.push_back(std::move(info));
        continue;
      }
    }

    // 6. Fallback: the job looks feasible in isolation for at least one
    //    vehicle, but the search still couldn't place it (competing jobs,
    //    precedence, exploration limits, …).
    info.reason = UnassignedReason::no_feasible_insertion;
    out.push_back(std::move(info));
  }

  return out;
}

} // namespace vroom::utils
