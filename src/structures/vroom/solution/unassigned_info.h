#ifndef UNASSIGNED_INFO_H
#define UNASSIGNED_INFO_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <optional>
#include <string>
#include <vector>

#include "structures/typedefs.h"

namespace vroom {

enum class UnassignedReason {
  no_vehicle_with_required_skills,
  time_window_infeasible,
  capacity_exceeded,
  max_travel_time_exceeded,
  route_duration_limit_exceeded,
  no_feasible_insertion,
};

// Per-reason detail payload. Only the fields relevant to the reason are
// populated; consumers should branch on `reason` before reading them.
struct UnassignedDetails {
  // no_vehicle_with_required_skills
  std::optional<std::vector<Skill>> required_skills;
  std::optional<std::vector<Id>> vehicles_missing_skills;

  // time_window_infeasible
  std::optional<UserDuration> earliest;
  std::optional<UserDuration> latest;
  std::optional<Id> closest_feasible_vehicle;
  std::optional<UserDuration> closest_feasible_arrival;
  std::optional<UserDuration> shortfall_seconds;

  // capacity_exceeded
  std::optional<unsigned> capacity_dimension;
  std::optional<Capacity> required_capacity;
  std::optional<Capacity> max_available_capacity;

  // max_travel_time_exceeded / route_duration_limit_exceeded
  std::optional<UserDuration> max_allowed_seconds;
  std::optional<UserDuration> would_require_seconds;
};

struct UnassignedInfo {
  UnassignedReason reason{UnassignedReason::no_feasible_insertion};
  UnassignedDetails details;
};

// Stable string form for JSON serialization. Keys match the RFC §4.5.2
// enumeration exactly so consumer downstream code can key on them.
inline std::string to_string(UnassignedReason reason) {
  switch (reason) {
    using enum UnassignedReason;
  case no_vehicle_with_required_skills:
    return "no_vehicle_with_required_skills";
  case time_window_infeasible:
    return "time_window_infeasible";
  case capacity_exceeded:
    return "capacity_exceeded";
  case max_travel_time_exceeded:
    return "max_travel_time_exceeded";
  case route_duration_limit_exceeded:
    return "route_duration_limit_exceeded";
  case no_feasible_insertion:
    return "no_feasible_insertion";
  }
  return "no_feasible_insertion";
}

} // namespace vroom

#endif
