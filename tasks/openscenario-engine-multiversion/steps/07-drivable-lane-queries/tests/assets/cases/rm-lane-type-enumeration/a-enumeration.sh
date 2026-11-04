#!/usr/bin/env bash
# Enumerating the lanes of a road section: the count query and the
# index-addressed id query both take a type mask, and the ids come back through
# an out parameter in a defined order.
source /tests/caselib.sh

ANY=-1
DRIVING=2
BORDER=64

# straight_500m road 1 declares seven lanes across the section, numbered
# outward from the reference line: 3 2 1 0 -1 -2 -3. The order is the order the
# enumeration reports, so a case can pin it exactly.
run_driver rm_lane_by_type "$FIXTURES/straight_500m.xodr" 1 250.0 "$ANY"
need_status 0 $? "rm_lane_by_type any"
need_kv init 0 "init"
need_kv n 7 "lane count with the any mask"
need_kv id_0 3 "lane id 0 of the enumeration"
need_kv id_1 2 "lane id 1 of the enumeration"
need_kv id_2 1 "lane id 2 of the enumeration"
need_kv id_3 0 "lane id 3 of the enumeration"
need_kv id_4 -1 "lane id 4 of the enumeration"
need_kv id_5 -2 "lane id 5 of the enumeration"
need_kv id_6 -3 "lane id 6 of the enumeration"
need_kv all_indices_ok 1 "every index below the count answers"

# The mask really selects, it does not merely filter the count.
run_driver rm_lane_by_type "$FIXTURES/straight_500m.xodr" 1 250.0 "$DRIVING"
need_status 0 $? "rm_lane_by_type driving"
need_kv n 2 "lane count with the driving mask"
need_kv id_0 1 "first driving lane"
need_kv id_1 -1 "second driving lane"

run_driver rm_lane_by_type "$FIXTURES/straight_500m.xodr" 1 250.0 "$BORDER"
need_status 0 $? "rm_lane_by_type border"
need_kv n 4 "lane count with the border mask"
need_kv id_0 3 "first border lane"
need_kv id_3 -3 "last border lane"

# An index at or past the count, and a negative index, are errors, and the
# caller's storage is not written. Only the SIGN of the status is asserted: the
# documents promise a failure is negative, never a magnitude, so pinning -1
# would fail a correct implementation that numbers its failure causes.
run_driver rm_lane_by_type "$FIXTURES/straight_500m.xodr" 1 250.0 "$ANY"
need_neg oob_rc "index equal to the count"
need_kv oob_untouched 1 "out parameter one past the end"
need_neg neg_rc "negative index"
need_kv survived 1 "driver completed"
exit 0
