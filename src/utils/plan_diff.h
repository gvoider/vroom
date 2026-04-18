#ifndef PLAN_DIFF_H
#define PLAN_DIFF_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../include/rapidjson/include/rapidjson/document.h"
#include "structures/typedefs.h"

namespace vroom::io {

// Busportal fork, M5 / F4. Structured diff of two solution JSONs. See
// RFC §4.4 — the HTTP wrapper (vroom-express) POSTs the two solutions
// to `/diff` and routes to the fork; the fork computes this struct and
// emits it back as JSON via the sibling `to_json(PlanDiff, ...)`.

enum class ShipmentDiffType {
  unchanged,
  time_changed,          // same vehicle, |arrival_after - arrival_before| > 60 s
  moved_vehicle,         // different vehicle
  assigned_to_unassigned,
  unassigned_to_assigned,
  added_to_problem,      // absent from `before` entirely
  removed_from_problem,  // absent from `after` entirely
};

std::string to_string(ShipmentDiffType t);

struct ShipmentDiff {
  Id shipment_id{0};
  ShipmentDiffType type{ShipmentDiffType::unchanged};
  std::optional<Id> before_vehicle;
  std::optional<UserDuration> before_arrival;
  std::optional<Id> after_vehicle;
  std::optional<UserDuration> after_arrival;
};

struct RouteDiff {
  Id vehicle_id{0};
  long distance_change_m{0};
  long duration_change_seconds{0};
  int shipment_count_change{0};
  long cost_change{0};
};

struct SummaryDiff {
  long total_cost_change{0};
  long total_distance_change_m{0};
  int total_unassigned_change{0};
};

struct PlanDiff {
  std::vector<ShipmentDiff> shipment_diffs;
  std::vector<RouteDiff> route_diffs;
  SummaryDiff summary_diff;
};

// Compute the diff between two solution JSONs. Both arguments must be
// VROOM solution documents (the shape produced by `bin/vroom -i ...`).
// Malformed input throws `vroom::Exception` with code 2 so the CLI
// wrapper can surface it as a 400-class response.
//
// Time-change threshold is 60 seconds per RFC §4.4.3.
PlanDiff compute_plan_diff(const rapidjson::Document& before,
                           const rapidjson::Document& after);

// Serialize the diff to a top-level JSON document matching the RFC
// §4.4.2 response shape.
rapidjson::Document to_json(const PlanDiff& diff);

void write_plan_diff(const PlanDiff& diff,
                     const std::string& output_file = "");

} // namespace vroom::io

#endif
