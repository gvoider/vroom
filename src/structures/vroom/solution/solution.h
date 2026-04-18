#ifndef SOLUTION_H
#define SOLUTION_H

/*

This file is part of VROOM.

Copyright (c) 2015-2025, Julien Coupey.
All rights reserved (see LICENSE).

*/

#include <string>
#include <vector>

#include "structures/vroom/job.h"
#include "structures/vroom/solution/route.h"
#include "structures/vroom/solution/summary.h"
#include "structures/vroom/solution/unassigned_info.h"

namespace vroom {

struct Solution {
  Summary summary;
  std::vector<Route> routes;
  std::vector<Job> unassigned;
  // Empty unless diagnostics is enabled on Input. When populated this
  // vector is parallel to `unassigned`: `unassigned_info[i]` describes
  // why `unassigned[i]` could not be served.
  std::vector<UnassignedInfo> unassigned_info;

  Solution(const Amount& zero_amount,
           std::vector<Route>&& routes,
           std::vector<Job>&& unassigned);
};

} // namespace vroom

#endif
