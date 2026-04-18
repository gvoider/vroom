/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <numeric>

#include "utils/co_location_dedup.h"

#include "structures/typedefs.h"
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

// Look up the user-scaled earliest TW-start for a pickup step by id.
// Returns 0 when the id isn't indexed (shouldn't happen on a well-formed
// solution) or when the job has no explicit TW (mainline default is an
// open window starting at 0).
UserDuration earliest_tw_start(const Input& input, Id pickup_id) {
  auto it = input.pickup_id_to_rank.find(pickup_id);
  if (it == input.pickup_id_to_rank.end()) {
    return 0;
  }
  const auto& job = input.jobs[it->second];
  if (job.tws.empty()) {
    return 0;
  }
  return static_cast<UserDuration>(job.tws.front().start / DURATION_FACTOR);
}

// Re-accumulate every field of `summary.cost_breakdown` from the routes.
// We re-sum unconditionally after any dedup adjustment because the
// targeted-field approach that shipped in M3 left the other buckets
// holding stale pre-dedup aggregates (bug 4 in inbox #7). Even though
// only `task` changes today, M4 (soft TWs) and M8 (published-vehicle
// deviation) will populate more buckets; re-accumulating everything
// keeps the sum-equals-cost invariant robust against those.
void reaccumulate_summary(Solution& sol) {
  CostBreakdown bd;
  UserCost cost = 0;
  UserDuration service = 0;
  for (const auto& route : sol.routes) {
    bd += route.cost_breakdown;
    cost += route.cost;
    service += route.service;
  }
  sol.summary.cost_breakdown = bd;
  sol.summary.cost = cost;
  sol.summary.service = service;
}

} // anonymous namespace

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

      // Identify the maximal consecutive run of pickup steps sharing
      // the anchor's group.
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
        // --- Bug 2 fix: the common arrival must respect every group
        // member's own earliest-start TW. Taking the max across
        // (anchor.arrival, every member's tws.front().start) satisfies
        // the RFC's "arrivals MUST be equal" clause without pushing
        // any member below its own hard TW-start.
        UserDuration common_arrival = anchor.arrival;
        for (std::size_t k = i; k < j; ++k) {
          common_arrival = std::max(common_arrival,
                                    earliest_tw_start(input, route.steps[k].id));
        }

        // The effective saving is the gap between the last member's
        // current arrival-after-its-service and the new common_arrival
        // plus max_service. When TW-start pressure has shifted
        // common_arrival forward it may eat into the raw service-sum
        // saving — we reuse the amount we actually gained.
        const UserDuration last_member_departure_pre =
          route.steps[j - 1].arrival + route.steps[j - 1].service;
        const UserDuration group_departure_post = common_arrival + max_service;
        if (group_departure_post > last_member_departure_pre) {
          // Our adjustment would lengthen the schedule, which can
          // happen on a pathological TW configuration. Leave the run
          // alone rather than pretend to save time we didn't.
          i = j;
          continue;
        }
        const UserDuration saving =
          last_member_departure_pre - group_departure_post;
        if (saving == 0) {
          i = j;
          continue;
        }
        route_saving += saving;

        // Collapse service. The pooled `max_service` is charged on the
        // LAST member of the group, not the first — this keeps the
        // upstream step-timing invariant
        //     arrival[k] - arrival[0] ==
        //         duration[k] + cumulative(setup+service+waiting)<k
        // intact. If we charged it on the first member, member 2's
        // zero-service arrival would mathematically land at
        // arrival[1] + max_service but we'd report arrival[1] (equal
        // to anchor's), producing a schema-level drift. Parking the
        // service on the last member means: every group member
        // arrives at `common_arrival` with service=0, and the
        // vehicle's cumulative-service pointer only advances by
        // `max_service` after the group's last step — matching how
        // downstream travel legs should be timed.
        //
        // --- Bug 1 nuance: step.arrival shifts by `saving`, but
        // step.duration does NOT. VROOM's contract (see
        // Route::check_timing_consistency) is that step.duration is
        // cumulative TRAVEL duration from route start — it doesn't
        // include setup/service/waiting. Dedup removes service, not
        // travel, so duration must stay put.
        for (std::size_t k = i; k < j - 1; ++k) {
          route.steps[k].service = 0;
          route.steps[k].arrival = common_arrival;
          route.steps[k].waiting_time = 0;
        }
        route.steps[j - 1].service = max_service;
        route.steps[j - 1].arrival = common_arrival;
        route.steps[j - 1].waiting_time = 0;

        // Shift every subsequent step's arrival earlier by the saving.
        // step.duration stays put (cumulative travel unchanged).
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
      // same-location steps is already 0 in the matrix. Only the
      // service-time aggregate changes here; route.duration stays put.
      route.service -= route_saving;

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

  // --- Bug 4 fix: re-accumulate every field of summary.cost_breakdown
  // (not just `task`), the top-level `cost`, and `service` from the
  // routes. The M3 implementation only updated `task` / `cost` /
  // `service`, leaving other breakdown buckets (`duration`, `distance`,
  // `fixed_vehicle`, `priority_bias`, soft-TW, published-vehicle)
  // holding pre-dedup aggregates. Today it happened to produce
  // correct totals because `task` was the only non-zero bucket being
  // touched; later milestones populate the others.
  reaccumulate_summary(sol);

  sol.summary.computing_times.co_location_savings_seconds = total_saving;
}

} // namespace vroom::utils
