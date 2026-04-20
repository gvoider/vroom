/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../include/rapidjson/include/rapidjson/prettywriter.h"
#include "../include/rapidjson/include/rapidjson/stringbuffer.h"
#include "../include/rapidjson/include/rapidjson/writer.h"

#include "structures/vroom/input/input.h"
#include "utils/counterfactual.h"
#include "utils/exception.h"
#include "utils/input_parser.h"
#include "utils/output_json.h"

namespace vroom::io {

namespace {

// First-one-wins enumeration per RFC §5.6.2.
const std::vector<std::string> WHAT_IF_KEYS = {
  "add_vehicles",
  "remove_vehicles",
  "relax_time_windows",
  "add_shipments",
  "remove_shipments",
};

std::string document_to_string(const rapidjson::Document& doc) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  doc.Accept(writer);
  return std::string(buf.GetString(), buf.GetSize());
}

long summary_cost_of(const rapidjson::Document& sol) {
  if (sol.HasMember("summary") && sol["summary"].IsObject() &&
      sol["summary"].HasMember("cost") && sol["summary"]["cost"].IsInt64()) {
    return sol["summary"]["cost"].GetInt64();
  }
  return 0;
}

int unassigned_shipment_count(const rapidjson::Document& sol) {
  if (!sol.HasMember("unassigned") || !sol["unassigned"].IsArray()) {
    return 0;
  }
  std::unordered_set<Id> seen;
  for (rapidjson::SizeType i = 0; i < sol["unassigned"].Size(); ++i) {
    const auto& e = sol["unassigned"][i];
    if (!e.HasMember("id") || !e["id"].IsUint64()) {
      continue;
    }
    if (e.HasMember("type") && e["type"].IsString() &&
        std::string(e["type"].GetString()) == "delivery") {
      continue;
    }
    seen.insert(e["id"].GetUint64());
  }
  return static_cast<int>(seen.size());
}

// Apply one what_if transformation to the mutable problem document.
// Returns the key that was applied (one of WHAT_IF_KEYS) or "none".
std::string apply_what_if(rapidjson::Document& problem,
                          const rapidjson::Value& what_if) {
  if (!what_if.IsObject()) {
    return "none";
  }
  auto& alloc = problem.GetAllocator();

  for (const auto& key : WHAT_IF_KEYS) {
    if (!what_if.HasMember(key.c_str())) {
      continue;
    }
    const auto& payload = what_if[key.c_str()];

    if (key == "add_vehicles") {
      if (!payload.IsArray()) {
        throw InputException("counterfactual: add_vehicles must be an array.");
      }
      if (!problem.HasMember("vehicles") || !problem["vehicles"].IsArray()) {
        rapidjson::Value empty(rapidjson::kArrayType);
        problem.AddMember("vehicles", empty, alloc);
      }
      auto& vehicles = problem["vehicles"];
      for (rapidjson::SizeType i = 0; i < payload.Size(); ++i) {
        rapidjson::Value copy(payload[i], alloc);
        vehicles.PushBack(copy, alloc);
      }
      return key;
    }

    if (key == "remove_vehicles") {
      if (!payload.IsArray()) {
        throw InputException(
          "counterfactual: remove_vehicles must be an array of ids.");
      }
      std::unordered_set<Id> drop;
      for (rapidjson::SizeType i = 0; i < payload.Size(); ++i) {
        if (!payload[i].IsUint64()) {
          throw InputException(
            "counterfactual: remove_vehicles ids must be unsigned integers.");
        }
        drop.insert(payload[i].GetUint64());
      }
      if (!problem.HasMember("vehicles") || !problem["vehicles"].IsArray()) {
        return key;
      }
      auto& vehicles = problem["vehicles"];
      rapidjson::Value kept(rapidjson::kArrayType);
      for (rapidjson::SizeType i = 0; i < vehicles.Size(); ++i) {
        const auto& v = vehicles[i];
        if (v.HasMember("id") && v["id"].IsUint64() &&
            drop.contains(v["id"].GetUint64())) {
          continue;
        }
        rapidjson::Value copy(v, alloc);
        kept.PushBack(copy, alloc);
      }
      problem["vehicles"].Swap(kept);
      return key;
    }

    if (key == "relax_time_windows") {
      if (!payload.IsArray()) {
        throw InputException(
          "counterfactual: relax_time_windows must be an array.");
      }
      auto relax_step_tws = [&](rapidjson::Value& step, long delta) {
        if (!step.IsObject() || !step.HasMember("time_windows") ||
            !step["time_windows"].IsArray()) {
          return;
        }
        auto& tws = step["time_windows"];
        for (rapidjson::SizeType i = 0; i < tws.Size(); ++i) {
          auto& tw = tws[i];
          if (!tw.IsArray() || tw.Size() != 2 || !tw[0].IsUint() ||
              !tw[1].IsUint()) {
            continue;
          }
          const long lo = static_cast<long>(tw[0].GetUint());
          const long hi = static_cast<long>(tw[1].GetUint());
          const long new_lo = std::max<long>(0, lo - delta);
          const long new_hi = hi + delta;
          tw[0].SetUint(static_cast<unsigned>(new_lo));
          tw[1].SetUint(static_cast<unsigned>(new_hi));
        }
      };

      if (!problem.HasMember("shipments") || !problem["shipments"].IsArray()) {
        return key;
      }
      auto& shipments = problem["shipments"];

      for (rapidjson::SizeType r = 0; r < payload.Size(); ++r) {
        const auto& req = payload[r];
        if (!req.IsObject() || !req.HasMember("shipment_id") ||
            !req["shipment_id"].IsUint64() || !req.HasMember("step") ||
            !req["step"].IsString() || !req.HasMember("delta_seconds") ||
            !req["delta_seconds"].IsNumber()) {
          throw InputException(
            "counterfactual: relax_time_windows entries must have "
            "shipment_id (uint), step (pickup|delivery), delta_seconds (int).");
        }
        const Id sid = req["shipment_id"].GetUint64();
        const std::string which = req["step"].GetString();
        const long delta = req["delta_seconds"].GetInt64();
        if (which != "pickup" && which != "delivery") {
          throw InputException(
            "counterfactual: relax_time_windows.step must be 'pickup' or "
            "'delivery'.");
        }
        for (rapidjson::SizeType s = 0; s < shipments.Size(); ++s) {
          auto& shipment = shipments[s];
          if (!shipment.HasMember(which.c_str())) {
            continue;
          }
          auto& step = shipment[which.c_str()];
          if (!step.IsObject() || !step.HasMember("id") ||
              !step["id"].IsUint64() || step["id"].GetUint64() != sid) {
            continue;
          }
          relax_step_tws(step, delta);
        }
      }
      return key;
    }

    if (key == "add_shipments") {
      if (!payload.IsArray()) {
        throw InputException("counterfactual: add_shipments must be an array.");
      }
      if (!problem.HasMember("shipments") || !problem["shipments"].IsArray()) {
        rapidjson::Value empty(rapidjson::kArrayType);
        problem.AddMember("shipments", empty, alloc);
      }
      auto& shipments = problem["shipments"];
      for (rapidjson::SizeType i = 0; i < payload.Size(); ++i) {
        rapidjson::Value copy(payload[i], alloc);
        shipments.PushBack(copy, alloc);
      }
      return key;
    }

    if (key == "remove_shipments") {
      if (!payload.IsArray()) {
        throw InputException(
          "counterfactual: remove_shipments must be an array of pickup ids.");
      }
      std::unordered_set<Id> drop;
      for (rapidjson::SizeType i = 0; i < payload.Size(); ++i) {
        if (!payload[i].IsUint64()) {
          throw InputException(
            "counterfactual: remove_shipments ids must be unsigned integers.");
        }
        drop.insert(payload[i].GetUint64());
      }
      if (!problem.HasMember("shipments") || !problem["shipments"].IsArray()) {
        return key;
      }
      auto& shipments = problem["shipments"];
      rapidjson::Value kept(rapidjson::kArrayType);
      for (rapidjson::SizeType i = 0; i < shipments.Size(); ++i) {
        const auto& sh = shipments[i];
        const auto& pickup = sh.HasMember("pickup") ? sh["pickup"] : sh;
        if (pickup.IsObject() && pickup.HasMember("id") &&
            pickup["id"].IsUint64() &&
            drop.contains(pickup["id"].GetUint64())) {
          continue;
        }
        rapidjson::Value copy(sh, alloc);
        kept.PushBack(copy, alloc);
      }
      problem["shipments"].Swap(kept);
      return key;
    }
  }
  return "none";
}

// Run a full parse + solve on a problem JSON string. Returns the
// solution as a rapidjson::Document (via to_json) along with the
// measured solve wall-clock in milliseconds.
rapidjson::Document solve_problem(const std::string& problem_json,
                                  const vroom::io::CLArgs& cl_args,
                                  long& solve_ms_out) {
  Input problem_instance(cl_args.servers,
                         cl_args.router,
                         cl_args.apply_TSPFix);
  vroom::io::parse(problem_instance,
                   problem_json,
                   cl_args.geometry,
                   cl_args.diagnostics);

  const auto t_start = std::chrono::high_resolution_clock::now();
  auto sol = problem_instance.solve(cl_args.nb_searches,
                                    cl_args.depth,
                                    cl_args.nb_threads,
                                    cl_args.timeout);
  const auto t_end = std::chrono::high_resolution_clock::now();
  solve_ms_out = std::chrono::duration_cast<std::chrono::milliseconds>(
                   t_end - t_start)
                   .count();

  // Mirror main.cpp's behavior for distances reporting.
  const bool report_distances =
    cl_args.geometry ||
    sol.summary.distance > 0; // conservative: emit when we have it.

  return vroom::io::to_json(sol, report_distances);
}

} // anonymous namespace

rapidjson::Document run_counterfactual(const std::string& input_json,
                                       const vroom::io::CLArgs& cl_args) {
  rapidjson::Document envelope;
  if (envelope.Parse(input_json.c_str()).HasParseError()) {
    throw InputException("counterfactual: input is not valid JSON.");
  }
  if (!envelope.IsObject() || !envelope.HasMember("problem") ||
      !envelope["problem"].IsObject()) {
    throw InputException(
      "counterfactual: input must be {\"problem\": {...}, \"what_if\": {...}}.");
  }

  // Pull the baseline problem out. We need two mutable copies — one
  // for the baseline solve (unchanged) and one for the what_if solve.
  rapidjson::Document baseline_problem;
  baseline_problem.CopyFrom(envelope["problem"], baseline_problem.GetAllocator());
  rapidjson::Document modified_problem;
  modified_problem.CopyFrom(envelope["problem"],
                            modified_problem.GetAllocator());

  std::string applied = "none";
  if (envelope.HasMember("what_if") && envelope["what_if"].IsObject()) {
    applied = apply_what_if(modified_problem, envelope["what_if"]);
  }

  long baseline_ms = 0;
  long modified_ms = 0;
  const auto baseline_sol =
    solve_problem(document_to_string(baseline_problem), cl_args, baseline_ms);
  const auto modified_sol =
    solve_problem(document_to_string(modified_problem), cl_args, modified_ms);

  const auto diff = compute_plan_diff(baseline_sol, modified_sol);

  ImprovementSummary imp;
  imp.additional_assigned =
    unassigned_shipment_count(baseline_sol) -
    unassigned_shipment_count(modified_sol);
  imp.cost_change =
    summary_cost_of(modified_sol) - summary_cost_of(baseline_sol);
  imp.new_total_cost = summary_cost_of(modified_sol);
  imp.solve_time_ms_baseline = baseline_ms;
  imp.solve_time_ms_modified = modified_ms;
  imp.applied_what_if = applied;

  rapidjson::Document out;
  out.SetObject();
  auto& alloc = out.GetAllocator();

  rapidjson::Value b_copy(baseline_sol, alloc);
  rapidjson::Value m_copy(modified_sol, alloc);
  out.AddMember("baseline_solution", b_copy, alloc);
  out.AddMember("modified_solution", m_copy, alloc);

  auto diff_doc = vroom::io::to_json(diff);
  rapidjson::Value diff_copy(diff_doc, alloc);
  out.AddMember("diff", diff_copy, alloc);

  rapidjson::Value impr(rapidjson::kObjectType);
  impr.AddMember("additional_assigned", imp.additional_assigned, alloc);
  impr.AddMember("cost_change", imp.cost_change, alloc);
  impr.AddMember("new_total_cost", imp.new_total_cost, alloc);
  impr.AddMember("solve_time_ms_baseline", imp.solve_time_ms_baseline, alloc);
  impr.AddMember("solve_time_ms_modified", imp.solve_time_ms_modified, alloc);
  {
    rapidjson::Value applied_v;
    applied_v.SetString(imp.applied_what_if.c_str(),
                        imp.applied_what_if.size(),
                        alloc);
    impr.AddMember("applied_what_if", applied_v, alloc);
  }
  out.AddMember("improvement", impr, alloc);

  return out;
}

void write_counterfactual(const rapidjson::Document& doc,
                          const std::string& output_file) {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  if (output_file.empty()) {
    std::cout << buffer.GetString() << std::endl;
  } else {
    std::ofstream out(output_file);
    if (!out) {
      throw InternalException("counterfactual: failed to open output file " +
                              output_file);
    }
    out << buffer.GetString();
  }
}

} // namespace vroom::io
