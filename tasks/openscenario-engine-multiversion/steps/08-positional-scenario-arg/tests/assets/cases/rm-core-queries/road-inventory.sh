#!/usr/bin/env bash
source /tests/caselib.sh

# The road manager enumerates the network it loaded and reports a length for
# every road in it.

run_driver rm_core "$FIXTURES/fabriksgatan.xodr" 2 -1 20
need_kv init 0 "road manager init"
need_kv nroads 16 "road count"
need_kv first_road_id 0 "first road id"
need_kv all_roads_have_length 1 "every road has a length"
need_close road_length 304.194 0.01 "road 2 length"
need_kv ndrivable 2 "drivable lanes on road 2 at s=20"
need_kv survived 1 "driver reached the end"
