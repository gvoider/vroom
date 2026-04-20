#ifndef COUNTERFACTUAL_H
#define COUNTERFACTUAL_H

/*

This file is part of VROOM (gvoider fork).

Copyright (c) 2015-2025, Julien Coupey.
Copyright (c) 2026, Busportal contributors.
All rights reserved (see LICENSE).

*/

#include <string>

#include "../include/rapidjson/include/rapidjson/document.h"
#include "structures/cl_args.h"
#include "utils/plan_diff.h"

namespace vroom::io {

// Busportal fork, M6 / F6. Counterfactual mode runs two solves of a
// problem — one as-given, one with a single `what_if` transformation
// applied — and returns both solutions, a plan diff, and an
// `improvement` summary. See RFC §5.6.

struct ImprovementSummary {
  int additional_assigned{0};
  long cost_change{0};
  long new_total_cost{0};
  long solve_time_ms_baseline{0};
  long solve_time_ms_modified{0};

  // Records which what_if key won the "first in enumeration order" rule
  // (or "none" when the what_if object is empty / unrecognized). Handy
  // for the dispatcher UI when sanity-checking "which knob was turned".
  std::string applied_what_if{"none"};
};

// Single-call entry point. Parses the outer `{problem, what_if}`
// payload, runs both solves via `Input::solve`, composes the response
// document. Throws `vroom::Exception` on malformed input or routing
// errors so main can surface via `write_to_json`.
rapidjson::Document run_counterfactual(const std::string& input_json,
                                       const vroom::io::CLArgs& cl_args);

void write_counterfactual(const rapidjson::Document& doc,
                          const std::string& output_file = "");

} // namespace vroom::io

#endif
