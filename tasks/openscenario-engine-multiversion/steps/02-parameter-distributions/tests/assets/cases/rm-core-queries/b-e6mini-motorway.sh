#!/usr/bin/env bash
# INHERITED. Same surface as a-straight-road.sh against a real multi-lane
# motorway geometry, where the numbers are not round and the world-XYH round
# trip does NOT return the lane it started from. Measured identical at
# v2.23.1 and v2.27.0.
source /tests/caselib.sh

run_driver rm_core "$FIXTURES/e6mini.xodr" 0 -1 100
need_status 0 $? "rm_core on e6mini.xodr"

need_kv init 0                     "RM_Init on a valid .xodr"
need_kv nroads 1                   "e6mini.xodr has one road"
need_kv first_road_id 0            "that road's id"
need_kv all_roads_have_length 1    "every road reports a positive length"
need_kv road_length 1464.434       "road length"
need_kv nlanes 6                   "lane count at s=100"

need_kv set_lane_ok 1              "RM_SetLanePosition succeeded"
need_kv pos_road 0                 "road id read back"
need_kv pos_lane -1                "lane id read back"
need_kv pos_s 100.000              "s read back"

need_kv xyh_ok 1                   "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 0           "world-XYH round trip stays on road 0"
# Deliberately pinned to -2 rather than -1. On e6mini the x/y produced for
# lane -1 at s=100 snaps to the neighbouring lane when fed back through the
# world-position setter, and that is the long-standing behaviour at both ends
# of this step. Asserting -1 here would encode a wish, not the contract.
need_kv roundtrip_lane -2          "world-XYH round trip snaps to lane -2"
need_kv survived 1                 "driver reached the end without aborting"
