#!/usr/bin/env bash
# A road network can be loaded from a buffer already in memory, without a file
# on disk, and the resulting network behaves exactly like one loaded from the
# path. The driver loads the same bytes both ways and compares what the two
# networks report.
source /tests/caselib.sh

STR="$FIXTURES/straight_500m.xodr"
FAB="$FIXTURES/fabriksgatan.xodr"
E6="$FIXTURES/e6mini.xodr"

# The driver reads the file itself, loads it by path to take a reference
# snapshot, then loads the identical bytes from memory.
run_driver rm_init_string "$STR" 1 250.0
need_status 0 $? "rm_init_string straight_500m"
need_kv file_init 0 "loading by path"
need_kv string_init 0 "loading from memory"
need_kv matches_file_load 1 "the two loaders produced the same network"
need_kv nroads 1 "road count"
need_kv road_length 500.000 "road length"
need_kv n_any 7 "lane count under the any mask"
need_kv n_driving 2 "lane count under the driving mask"
need_close lane_width 3.07 0.01 "lane width from the memory-loaded network"
need_close pos_x 250.0 0.01 "position along the road"
need_close pos_y -1.535 0.01 "lateral position"

# A larger, many-road network too, so "matches the file load" is not a
# coincidence of a single-road file.
run_driver rm_init_string "$FAB" 0 20.0
need_status 0 $? "rm_init_string fabriksgatan"
need_kv string_init 0 "loading a many-road network from memory"
need_kv matches_file_load 1 "many-road network matches the file load"
need_kv nroads 16 "road count of the many-road network"
need_close road_length 93.661 0.01 "length of the addressed road"
need_close lane_width 3.5 0.01 "lane width from the memory-loaded network"

run_driver rm_init_string "$E6" 0 100.0
need_status 0 $? "rm_init_string e6mini"
need_kv matches_file_load 1 "the widest network matches the file load"
need_kv n_any 15 "lane count of the widest network"

# Text that is not a road network is refused with a status, not accepted into a
# silently empty manager and not a crash. The driver runs both negative cases on
# every invocation, so they are read back from the last run; that the driver got
# as far as printing `survived` is itself the "no crash" half of the check.
need_neg garbage_rc "text that is not an OpenDRIVE document"
need_neg empty_rc "an empty buffer"

need_kv survived 1 "driver completed"
exit 0
