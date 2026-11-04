#!/usr/bin/env bash
# MERGED CASE. This file is two cases that used to live side by side in this
# group, a-straight-road.sh and b-e6mini-motorway.sh, folded into one file to
# stay inside the package's per-step case budget. Nothing was dropped: every
# assertion and every comment from both halves is below, in its original
# order, and each half still makes its own driver run against its own fixture.
# Where the second half's comment refers to "a-straight-road.sh" it now means
# the first half of this same file.
# test.sh.in scores the GROUP, not the case file: `score` is the share-weighted
# fraction of the SCORED groups that passed, and the case count never enters
# that arithmetic -- so merging two files into one changes no weight and no
# denominator. What it does cost is localisation. A failure in either half now
# fails the whole file and therefore the whole group, and groups.txt's count
# column counts .sh files on disk, so this file is ONE counted case carrying
# the assertions of two.
#
# ---------------------------------------------------------------------------
# Half 1 (was a-straight-road.sh)
# ---------------------------------------------------------------------------
# INHERITED. The road-manager core query surface predates this step and must
# keep behaving identically across it. Every value below was read from real
# binaries at BOTH v2.27.5 and v2.31.10 and is byte-identical at the two tags,
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
need_close road_length 500.000 0.001    "road length"
need_kv nlanes 2                   "lane count at s=250"

need_kv handle 0                   "first RM position handle"
need_kv set_lane_ok 1              "RM_SetLanePosition succeeded"
need_kv get_position 1             "RM_GetPositionData returned data"
need_kv pos_road 1                 "road id read back"
need_kv pos_lane -1                "lane id read back"
need_close pos_s 250.000 0.001          "s read back"

# Round trip: take the x/y/h the lane position produced, hand it back through
# RM_SetWorldXYHPosition on a second handle, and require the same lane.
need_kv xyh_ok 1                   "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 1           "world-XYH round trip lands on the same road"
need_kv roundtrip_lane -1          "world-XYH round trip lands on the same lane"
need_kv survived 1                 "driver reached the end without aborting"

# ---------------------------------------------------------------------------
# Half 2 (was b-e6mini-motorway.sh)
# ---------------------------------------------------------------------------
# INHERITED. Same surface as a-straight-road.sh against a real multi-lane
# motorway geometry, where the numbers are not round and the world-XYH round
# trip does NOT return the lane it started from. Measured identical at
# v2.27.5 and v2.31.10.

run_driver rm_core "$FIXTURES/e6mini.xodr" 0 -1 100
need_status 0 $? "rm_core on e6mini.xodr"

need_kv init 0                     "RM_Init on a valid .xodr"
need_kv nroads 1                   "e6mini.xodr has one road"
need_kv first_road_id 0            "that road's id"
need_kv all_roads_have_length 1    "every road reports a positive length"
need_close road_length 1464.434 0.001   "road length"
need_kv nlanes 6                   "lane count at s=100"

need_kv set_lane_ok 1              "RM_SetLanePosition succeeded"
need_kv pos_road 0                 "road id read back"
need_kv pos_lane -1                "lane id read back"
need_close pos_s 100.000 0.001          "s read back"

need_kv xyh_ok 1                   "RM_SetWorldXYHPosition succeeded"
need_kv roundtrip_road 0           "world-XYH round trip stays on road 0"
# Deliberately pinned to -2 rather than -1. On e6mini the x/y produced for
# lane -1 at s=100 snaps to the neighbouring lane when fed back through the
# world-position setter, and that is the long-standing behaviour at both ends
# of this step. Asserting -1 here would encode a wish, not the contract.
need_kv roundtrip_lane -2          "world-XYH round trip snaps to lane -2"
need_kv survived 1                 "driver reached the end without aborting"
