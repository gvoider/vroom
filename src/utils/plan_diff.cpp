/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "../include/rapidjson/include/rapidjson/prettywriter.h"
#include "../include/rapidjson/include/rapidjson/stringbuffer.h"

#include "utils/exception.h"
#include "utils/plan_diff.h"

namespace vroom::io {

namespace {

// Per RFC §4.4.3: same-vehicle arrival shifts under this threshold are
// `unchanged`; above, `time_changed`.
constexpr UserDuration TIME_CHANGED_THRESHOLD_SECONDS = 60;

struct ShipmentSnapshot {
  // If present, the shipment was served in this solution and these are
  // the vehicle / arrival it landed on. If absent, the shipment
  // appeared in the solution's `unassigned` array.
  std::optional<Id> vehicle_id;
  std::optional<UserDuration> arrival;
};

struct SolutionSnapshot {
  // Shipment id → snapshot. Covers every shipment present in either
  // the routes or the unassigned list. Missing ids mean the shipment
  // was absent from the solution entirely (removed_from_problem /
  // added_to_problem cases).
  std::unordered_map<Id, ShipmentSnapshot> shipments;

  // Per-vehicle totals (only for vehicles that actually have a route).
  struct VehicleTotals {
    long distance_m{0};
    long duration_s{0};
    int shipment_count{0};
    long cost{0};
  };
  std::unordered_map<Id, VehicleTotals> vehicles;

  long total_cost{0};
  long total_distance_m{0};
  int unassigned_count{0};
};

bool is_solution_shaped(const rapidjson::Value& doc) {
  return doc.IsObject() && doc.HasMember("routes") &&
         doc["routes"].IsArray() && doc.HasMember("unassigned") &&
         doc["unassigned"].IsArray();
}

// Extract a per-shipment snapshot plus per-vehicle + summary totals.
// Steps are identified as "shipment-pickup" via `type == "pickup"` or
// `type == "job"` (a single-job is a degenerate shipment for our
// diffing purposes). Deliveries are skipped — they follow the pickup
// and their arrival doesn't carry independent information the
// dispatcher cares about in the reshuffling panel.
SolutionSnapshot snapshot_of(const rapidjson::Value& sol,
                             const std::string& which) {
  if (!is_solution_shaped(sol)) {
    throw InputException(
      "plan diff: `" + which +
      "` must be a VROOM solution JSON with `routes` and `unassigned`.");
  }

  SolutionSnapshot snap;

  const auto& routes = sol["routes"];
  for (rapidjson::SizeType r = 0; r < routes.Size(); ++r) {
    const auto& route = routes[r];
    if (!route.HasMember("vehicle") || !route["vehicle"].IsUint64()) {
      continue;
    }
    const Id vid = route["vehicle"].GetUint64();

    auto& vt = snap.vehicles[vid];
    vt.distance_m =
      (route.HasMember("distance") && route["distance"].IsInt64())
        ? route["distance"].GetInt64()
        : 0;
    vt.duration_s =
      (route.HasMember("duration") && route["duration"].IsInt64())
        ? route["duration"].GetInt64()
        : 0;
    vt.cost = (route.HasMember("cost") && route["cost"].IsInt64())
                ? route["cost"].GetInt64()
                : 0;

    if (!route.HasMember("steps") || !route["steps"].IsArray()) {
      continue;
    }
    for (rapidjson::SizeType s = 0; s < route["steps"].Size(); ++s) {
      const auto& step = route["steps"][s];
      if (!step.HasMember("type") || !step["type"].IsString()) {
        continue;
      }
      const std::string type = step["type"].GetString();
      if (type != "pickup" && type != "job") {
        continue;
      }
      if (!step.HasMember("id") || !step["id"].IsUint64()) {
        continue;
      }
      const Id sid = step["id"].GetUint64();
      const UserDuration arrival =
        step.HasMember("arrival") && step["arrival"].IsUint()
          ? step["arrival"].GetUint()
          : 0;
      snap.shipments[sid] = ShipmentSnapshot{vid, arrival};
      ++vt.shipment_count;
    }
  }

  // Unassigned entries. A shipment contributes TWO entries (pickup +
  // delivery) to the array; a single-job contributes one. Count
  // distinct shipments (identified by their pickup / job entry) so the
  // summary delta matches the dispatcher's "how many shipments fell
  // off the plan" mental model.
  const auto& unassigned = sol["unassigned"];
  std::unordered_set<Id> unassigned_shipment_ids;
  for (rapidjson::SizeType u = 0; u < unassigned.Size(); ++u) {
    const auto& entry = unassigned[u];
    if (!entry.HasMember("id") || !entry["id"].IsUint64()) {
      continue;
    }
    const Id sid = entry["id"].GetUint64();
    // Skip delivery entries — the pickup / job side is canonical.
    if (entry.HasMember("type") && entry["type"].IsString() &&
        std::string(entry["type"].GetString()) == "delivery") {
      continue;
    }
    unassigned_shipment_ids.insert(sid);
    if (snap.shipments.find(sid) == snap.shipments.end()) {
      snap.shipments[sid] = ShipmentSnapshot{};
    }
  }
  snap.unassigned_count = static_cast<int>(unassigned_shipment_ids.size());

  // Summary totals.
  if (sol.HasMember("summary") && sol["summary"].IsObject()) {
    const auto& sum = sol["summary"];
    if (sum.HasMember("cost") && sum["cost"].IsInt64()) {
      snap.total_cost = sum["cost"].GetInt64();
    }
    if (sum.HasMember("distance") && sum["distance"].IsInt64()) {
      snap.total_distance_m = sum["distance"].GetInt64();
    }
  }

  return snap;
}

long diff_or_zero(const std::optional<long>& after,
                  const std::optional<long>& before) {
  return after.value_or(0) - before.value_or(0);
}

} // anonymous namespace

std::string to_string(ShipmentDiffType t) {
  switch (t) {
    using enum ShipmentDiffType;
  case unchanged:
    return "unchanged";
  case time_changed:
    return "time_changed";
  case moved_vehicle:
    return "moved_vehicle";
  case assigned_to_unassigned:
    return "assigned_to_unassigned";
  case unassigned_to_assigned:
    return "unassigned_to_assigned";
  case added_to_problem:
    return "added_to_problem";
  case removed_from_problem:
    return "removed_from_problem";
  }
  return "unchanged";
}

PlanDiff compute_plan_diff(const rapidjson::Document& before,
                           const rapidjson::Document& after) {
  const auto snap_before = snapshot_of(before, "before");
  const auto snap_after = snapshot_of(after, "after");

  PlanDiff diff;

  // Union of shipment ids seen in either snapshot.
  std::unordered_set<Id> shipment_ids;
  for (const auto& [id, _] : snap_before.shipments) {
    shipment_ids.insert(id);
  }
  for (const auto& [id, _] : snap_after.shipments) {
    shipment_ids.insert(id);
  }

  std::vector<Id> sorted_ids(shipment_ids.begin(), shipment_ids.end());
  std::sort(sorted_ids.begin(), sorted_ids.end());

  for (Id sid : sorted_ids) {
    ShipmentDiff sd;
    sd.shipment_id = sid;

    const auto in_before = snap_before.shipments.find(sid);
    const auto in_after = snap_after.shipments.find(sid);
    const bool present_before = in_before != snap_before.shipments.end();
    const bool present_after = in_after != snap_after.shipments.end();

    if (!present_before) {
      sd.type = ShipmentDiffType::added_to_problem;
      if (in_after->second.vehicle_id.has_value()) {
        sd.after_vehicle = in_after->second.vehicle_id;
        sd.after_arrival = in_after->second.arrival;
      }
    } else if (!present_after) {
      sd.type = ShipmentDiffType::removed_from_problem;
      if (in_before->second.vehicle_id.has_value()) {
        sd.before_vehicle = in_before->second.vehicle_id;
        sd.before_arrival = in_before->second.arrival;
      }
    } else {
      const auto& bsnap = in_before->second;
      const auto& asnap = in_after->second;
      const bool assigned_before = bsnap.vehicle_id.has_value();
      const bool assigned_after = asnap.vehicle_id.has_value();

      if (assigned_before) {
        sd.before_vehicle = bsnap.vehicle_id;
        sd.before_arrival = bsnap.arrival;
      }
      if (assigned_after) {
        sd.after_vehicle = asnap.vehicle_id;
        sd.after_arrival = asnap.arrival;
      }

      if (!assigned_before && !assigned_after) {
        sd.type = ShipmentDiffType::unchanged;
      } else if (assigned_before && !assigned_after) {
        sd.type = ShipmentDiffType::assigned_to_unassigned;
      } else if (!assigned_before && assigned_after) {
        sd.type = ShipmentDiffType::unassigned_to_assigned;
      } else if (*bsnap.vehicle_id != *asnap.vehicle_id) {
        sd.type = ShipmentDiffType::moved_vehicle;
      } else {
        const long a_arr = static_cast<long>(asnap.arrival.value_or(0));
        const long b_arr = static_cast<long>(bsnap.arrival.value_or(0));
        const long d = a_arr - b_arr;
        if (std::labs(d) > TIME_CHANGED_THRESHOLD_SECONDS) {
          sd.type = ShipmentDiffType::time_changed;
        } else {
          sd.type = ShipmentDiffType::unchanged;
        }
      }
    }

    diff.shipment_diffs.push_back(sd);
  }

  // Route diffs — vehicles present in either snapshot.
  std::unordered_set<Id> vehicle_ids;
  for (const auto& [vid, _] : snap_before.vehicles) {
    vehicle_ids.insert(vid);
  }
  for (const auto& [vid, _] : snap_after.vehicles) {
    vehicle_ids.insert(vid);
  }
  std::vector<Id> sorted_vehicles(vehicle_ids.begin(), vehicle_ids.end());
  std::sort(sorted_vehicles.begin(), sorted_vehicles.end());

  for (Id vid : sorted_vehicles) {
    RouteDiff rd;
    rd.vehicle_id = vid;

    const auto before_it = snap_before.vehicles.find(vid);
    const auto after_it = snap_after.vehicles.find(vid);

    std::optional<long> b_dist, a_dist, b_dur, a_dur, b_cost, a_cost;
    int b_count = 0, a_count = 0;
    if (before_it != snap_before.vehicles.end()) {
      b_dist = before_it->second.distance_m;
      b_dur = before_it->second.duration_s;
      b_cost = before_it->second.cost;
      b_count = before_it->second.shipment_count;
    }
    if (after_it != snap_after.vehicles.end()) {
      a_dist = after_it->second.distance_m;
      a_dur = after_it->second.duration_s;
      a_cost = after_it->second.cost;
      a_count = after_it->second.shipment_count;
    }

    rd.distance_change_m = diff_or_zero(a_dist, b_dist);
    rd.duration_change_seconds = diff_or_zero(a_dur, b_dur);
    rd.cost_change = diff_or_zero(a_cost, b_cost);
    rd.shipment_count_change = a_count - b_count;

    diff.route_diffs.push_back(rd);
  }

  // Summary diff.
  diff.summary_diff.total_cost_change =
    snap_after.total_cost - snap_before.total_cost;
  diff.summary_diff.total_distance_change_m =
    snap_after.total_distance_m - snap_before.total_distance_m;
  diff.summary_diff.total_unassigned_change =
    snap_after.unassigned_count - snap_before.unassigned_count;

  return diff;
}

namespace {

void write_shipment_diff(const ShipmentDiff& sd,
                         rapidjson::Value& out,
                         rapidjson::Document::AllocatorType& alloc) {
  out.SetObject();
  out.AddMember("shipment_id", sd.shipment_id, alloc);
  {
    const auto type_str = to_string(sd.type);
    rapidjson::Value tv;
    tv.SetString(type_str.c_str(), type_str.size(), alloc);
    out.AddMember("type", tv, alloc);
  }
  auto emit_side = [&](const char* key,
                       const std::optional<Id>& vid,
                       const std::optional<UserDuration>& arr) {
    if (!vid.has_value() && !arr.has_value()) {
      return;
    }
    rapidjson::Value side(rapidjson::kObjectType);
    if (vid.has_value()) {
      side.AddMember("vehicle_id", *vid, alloc);
    }
    if (arr.has_value()) {
      side.AddMember("arrival", *arr, alloc);
    }
    rapidjson::Value k;
    k.SetString(key, alloc);
    out.AddMember(k, side, alloc);
  };
  emit_side("before", sd.before_vehicle, sd.before_arrival);
  emit_side("after", sd.after_vehicle, sd.after_arrival);
}

} // anonymous namespace

rapidjson::Document to_json(const PlanDiff& diff) {
  rapidjson::Document doc;
  doc.SetObject();
  auto& alloc = doc.GetAllocator();

  rapidjson::Value shipment_diffs(rapidjson::kArrayType);
  for (const auto& sd : diff.shipment_diffs) {
    rapidjson::Value entry;
    write_shipment_diff(sd, entry, alloc);
    shipment_diffs.PushBack(entry, alloc);
  }
  doc.AddMember("shipment_diffs", shipment_diffs, alloc);

  rapidjson::Value route_diffs(rapidjson::kArrayType);
  for (const auto& rd : diff.route_diffs) {
    rapidjson::Value entry(rapidjson::kObjectType);
    entry.AddMember("vehicle_id", rd.vehicle_id, alloc);
    entry.AddMember("distance_change_m", rd.distance_change_m, alloc);
    entry.AddMember("duration_change_seconds",
                    rd.duration_change_seconds,
                    alloc);
    entry.AddMember("shipment_count_change", rd.shipment_count_change, alloc);
    entry.AddMember("cost_change", rd.cost_change, alloc);
    route_diffs.PushBack(entry, alloc);
  }
  doc.AddMember("route_diffs", route_diffs, alloc);

  rapidjson::Value sdd(rapidjson::kObjectType);
  sdd.AddMember("total_cost_change",
                diff.summary_diff.total_cost_change,
                alloc);
  sdd.AddMember("total_distance_change_m",
                diff.summary_diff.total_distance_change_m,
                alloc);
  sdd.AddMember("total_unassigned_change",
                diff.summary_diff.total_unassigned_change,
                alloc);
  doc.AddMember("summary_diff", sdd, alloc);

  return doc;
}

void write_plan_diff(const PlanDiff& diff, const std::string& output_file) {
  const auto doc = to_json(diff);
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  if (output_file.empty()) {
    std::cout << buffer.GetString() << std::endl;
  } else {
    std::ofstream out(output_file);
    if (!out) {
      throw InternalException("plan diff: failed to open output file " +
                              output_file);
    }
    out << buffer.GetString();
  }
}

} // namespace vroom::io
