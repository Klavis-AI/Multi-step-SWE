#!/usr/bin/env bash
# INHERITED. The road-manager core query surface predates this step and must
# keep behaving identically across it. Every value below was read from real
# binaries at BOTH v2.23.1 and v2.27.0 and is byte-identical at the two tags,
# so a solution that regresses any of it has broken something it was not asked
# to touch. A straight single-road network is the simplest case that still
# exercises id lookup, length, lane count, lane-position set/get and the
# world-XYH round trip.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/straight_500m.xodr" 1 -1 250
need_status 0 $? "rm_core on straight_500m.xodr"

need_kv init 0                     "RM_Init on a valid .xodr"
need_kv nroads 1                   "straight_500m.xodr has exactly one road"
need_kv first_road_id 1            "that road's id"
need_kv all_roads_have_length 1    "every road reports a positive length"
need_kv road_length 500.000        "road length"
need_kv nlanes 2                   "lane count at s=250"

need_kv handle 0                   "first RM position handle"
need_kv set_lane_ok 1              "RM_SetLanePosition succeeded"
need_kv get_position 1             "RM_GetPositionData returned data"
need_kv pos_road 1                 "road id read back"
need_kv pos_lane -1                "lane id read back"
need_kv pos_s 250.000              "s read back"

# Round trip: take the x/y/h the lane position produced, hand it back through
# RM_SetWorldXYHPosition on a second handle, and require the same lane.
need_kv xyh_ok 1                   "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 1           "world-XYH round trip lands on the same road"
need_kv roundtrip_lane -1          "world-XYH round trip lands on the same lane"
need_kv survived 1                 "driver reached the end without aborting"
