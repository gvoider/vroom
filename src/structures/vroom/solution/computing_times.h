#ifndef COMPUTING_TIMES_H
#define COMPUTING_TIMES_H

/*

This file is part of VROOM.

Copyright (c) 2015-2025, Julien Coupey.
All rights reserved (see LICENSE).

*/

#include "structures/typedefs.h"

namespace vroom {

struct ComputingTimes {
  // Computing times in milliseconds.
  UserDuration loading{0};
  UserDuration solving{0};
  UserDuration routing{0};

  // Busportal fork, M3 / F1. Total service time (in seconds, not ms)
  // saved across all routes by the co-location dedup pass. RFC §4.1.4
  // places this key under computing_times; the name suffix "_seconds"
  // is intentional because the rest of computing_times is ms and we
  // don't want consumers to mix units.
  UserDuration co_location_savings_seconds{0};

  ComputingTimes();
};

} // namespace vroom

#endif
