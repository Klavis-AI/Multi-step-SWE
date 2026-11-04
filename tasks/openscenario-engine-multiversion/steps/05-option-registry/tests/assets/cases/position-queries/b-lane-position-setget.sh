#!/usr/bin/env bash
# Inherited. RM_SetLanePosition must remain stable: a lane position (road 0,
# lane -1, s=30) must resolve onto the requested road, close to the requested s.
source /tests/caselib.sh

cp "$FIXTURES/e6mini.xodr" "$WORK/e6mini.xodr"

run_driver d_positions e6mini.xodr
need_status 0 $? "d_positions"

need_kv setlane_ge0 1 "lane position set"
need_kv lane_roadid 0 "lane position resolves onto the requested road"
need_kv lane_s_close 1 "lane position s is close to the requested 30 m"
need_kv survived 1 "driver completed"
exit 0
