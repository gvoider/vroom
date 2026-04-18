#!/usr/bin/env python3
"""Generate the self-contained 30-shipment synthetic fixture.

Writes a VROOM problem JSON to stdout with:
  - 3 vehicles (buses with 4-seat + 3-suitcase capacity, per_hour costs)
  - 28 shipments: 20 normal + 5 co-located at a shared stop + 3 with
    confirmed tighter TWs and higher priority
  - Embedded `matrices.auto.durations` computed from a haversine-like
    approximation (300 km/h effective bus speed over straight line)
  - Hard TWs anchored around a notional 15:00 departure (= 54000 sec)
  - A subset of shipments carries `soft_time_window` (F2) so the
    resulting fixture exercises the M4 shift-late pass on a realistic
    size. Others omit the tag to prove mainline compatibility.

Determinism: seeded RNG, re-runs produce identical output.

Usage:
  python3 scripts/gen-synthetic-30.py > tests/fixtures/regression/problem-synthetic-30.json
"""

import json
import math
import random

random.seed(42)

LVIV_STATION = (49.8411, 24.0315)
SHARED_STOP = (50.0, 25.3)
SPEED_KMH = 300  # exaggerated, keeps the synthetic problem tractable

CONFIRMED_TW = (53100, 54000)  # [-15m, 0m] around 15:00
FLEXIBLE_TW = (43200, 61200)   # [-180m, +120m] around 15:00
DELIVERY_TW = (46800, 61200)


def haversine_m(a, b):
    lat1, lon1 = a
    lat2, lon2 = b
    r = 6371000.0
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    h = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2
    return int(2 * r * math.asin(math.sqrt(h)))


def travel_s(a, b):
    meters = haversine_m(a, b)
    return int(round(meters / (SPEED_KMH * 1000.0 / 3600.0)))


def random_location():
    # Western Ukraine bounding box (matches the env doc's observed range).
    return (random.uniform(49.0, 51.0), random.uniform(23.0, 27.0))


def make_shipment(i, pickup_loc_idx, confirmed=False, co_located_group=None,
                  soft_late=False):
    hard_tw = CONFIRMED_TW if confirmed else FLEXIBLE_TW
    pickup = {
        "id": i,
        "location_index": pickup_loc_idx,
        "service": 60,
        "time_windows": [[hard_tw[0], hard_tw[1]]],
    }
    if co_located_group:
        pickup["co_located_group"] = co_located_group
    if soft_late:
        # Prefer arrival in the last 10 minutes of the hard window.
        pickup["soft_time_window"] = {
            "preferred": [hard_tw[1] - 600, hard_tw[1]],
            "cost_per_second_before": 0.5,
            "cost_per_second_after": 0,
        }
    return {
        "pickup": pickup,
        "delivery": {
            "id": i,
            "location_index": 0,  # Lviv station
            "service": 60,
            "time_windows": [[DELIVERY_TW[0], DELIVERY_TW[1]]],
        },
        "amount": [random.randint(1, 3), random.randint(0, 2)],
        "priority": 100 if confirmed else 10,
    }


def main():
    # Build the location list: index 0 = station, then per-shipment
    # pickup coords. Co-located shipments share a single location index.
    locations = [LVIV_STATION]

    shipments = []
    # 20 regular shipments, random coords
    for i in range(1, 21):
        locations.append(random_location())
        shipments.append(make_shipment(i, pickup_loc_idx=len(locations) - 1))

    # 5 co-located shipments sharing a synthetic stop
    locations.append(SHARED_STOP)
    shared_idx = len(locations) - 1
    for i in range(21, 26):
        # Half the co-located carry soft_time_window (late-preferred).
        shipments.append(
            make_shipment(i, pickup_loc_idx=shared_idx,
                          co_located_group="stop:vs-shared",
                          soft_late=(i % 2 == 0)))

    # 3 confirmed (tighter TW + higher priority)
    for i in range(26, 29):
        locations.append(random_location())
        shipments.append(
            make_shipment(i, pickup_loc_idx=len(locations) - 1, confirmed=True))

    # Durations matrix: travel time between every pair of locations.
    n = len(locations)
    durations = [[0] * n for _ in range(n)]
    for a in range(n):
        for b in range(n):
            if a == b:
                continue
            durations[a][b] = travel_s(locations[a], locations[b])

    problem = {
        "vehicles": [
            {
                "id": v,
                "profile": "auto",
                "start_index": 0,
                "end_index": 0,
                "capacity": [4, 3],
                "time_window": [43200, 68400],  # 12:00 to 19:00
                "costs": {"fixed": 0, "per_hour": 3600, "per_task_hour": 0},
            }
            for v in range(1, 4)
        ],
        "shipments": shipments,
        "matrices": {
            "auto": {
                "durations": durations,
            }
        },
    }

    print(json.dumps(problem, indent=2))


if __name__ == "__main__":
    main()
