#!/usr/bin/env bash
# The any-driving convenience query is a thin, exactly-specified wrapper over
# the general enumeration: same count, same ids, same order, and the same error
# behaviour. A case can therefore cross-check the two against each other.
source /tests/caselib.sh

E6="$FIXTURES/e6mini.xodr"
STR="$FIXTURES/straight_500m.xodr"

# The count and the ids agree with the general query under the mask that means
# the same thing, on two networks with different lane counts.
run_driver rm_drivable "$E6" 0 100.0
need_status 0 $? "rm_drivable e6mini"
need_kv init 0 "init"
need_kv n_drivable 6 "drivable lane count"
need_kv n_mask_driving 6 "count under the equivalent general mask"
need_kv agrees_with_mask 1 "the two queries agree"
need_kv n_any 15 "count under the any mask"
need_kv drivable_fewer_than_any 1 "drivable is a strict subset of any"
need_kv d_id_0 4 "first drivable lane"
need_kv d_id_1 3 "second drivable lane"
need_kv d_id_2 2 "third drivable lane"
need_kv d_id_3 -2 "fourth drivable lane"
need_kv d_id_4 -3 "fifth drivable lane"
need_kv d_id_5 -4 "sixth drivable lane"

run_driver rm_drivable "$STR" 1 250.0
need_status 0 $? "rm_drivable straight_500m"
need_kv n_drivable 2 "drivable lane count on the narrow road"
need_kv agrees_with_mask 1 "the two queries agree on the narrow road"
need_kv d_id_0 1 "first drivable lane on the narrow road"
need_kv d_id_1 -1 "second drivable lane on the narrow road"

# The same failure behaviour as the general enumeration. The two count queries
# are asserted by SIGN, not by magnitude: the requirements promise that an
# undefined road id is a failure and the return-convention note promises a
# failure is negative, but no document fixes the value, so pinning -1 would
# score a choice the candidate was never told to make.
need_neg oob_rc "index equal to the count"
need_kv oob_untouched 1 "out parameter one past the end"
need_neg bad_road_n "count for an undefined road id"
need_neg bad_road_id_rc "id query for an undefined road id"
need_neg pre_init_n "count before the network was loaded"

# A failing call settles that it has failed before it writes anything, so it
# still answers with a status when there is no destination to write to. The
# driver reaching `survived` is what proves it did not fault instead.
need_neg oob_null_rc "index past the end with no destination"
need_neg bad_road_null_rc "undefined road id with no destination"

need_kv survived 1 "driver completed"
exit 0
