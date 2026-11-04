#!/usr/bin/env bash
# The type mask is a bitmask over lane types, not an equality test and not a
# hardcoded "drivable" filter. The same road section answers differently for
# every mask, and a mask selecting a type the section does not carry answers
# with a count of zero rather than with an error.
source /tests/caselib.sh

E6="$FIXTURES/e6mini.xodr"

# e6mini road 0 at s=100 is the widest section in the fixture set: fifteen
# lanes under the all-types mask, six of which match ANY_DRIVING. The gap is
# the section's non-driving types plus its declared-driving reference line: the
# loader normalises lane 0's type to none, and none (bit 0) is a member of the
# all-types mask, so the reference line is counted by `-1` and by nothing else.
run_driver rm_lane_by_type "$E6" 0 100.0 -1
need_status 0 $? "rm_lane_by_type any"
need_kv n 15 "lane count with the any mask"

# ANY_DRIVING selects the driving surface and nothing else.
run_driver rm_lane_by_type "$E6" 0 100.0 1966594
need_status 0 $? "rm_lane_by_type any-driving"
need_kv n 6 "lane count with the any-driving mask"
need_kv id_0 4 "first lane of the any-driving set"
need_kv id_1 3 "second lane of the any-driving set"
need_kv id_5 -4 "last lane of the any-driving set"

# ANY_ROAD is a different mask with a different answer, so the mask cannot be
# ignored.
run_driver rm_lane_by_type "$E6" 0 100.0 1966726
need_status 0 $? "rm_lane_by_type any-road"
need_kv n 8 "lane count with the any-road mask"

# A single type bit, and one the section does not carry at all.
run_driver rm_lane_by_type "$E6" 0 100.0 4
need_status 0 $? "rm_lane_by_type stop"
need_kv n 2 "lane count with the stop bit"
need_kv id_0 5 "first stop lane"
need_kv id_1 -5 "second stop lane"

run_driver rm_lane_by_type "$E6" 0 100.0 32
need_status 0 $? "rm_lane_by_type sidewalk"
need_kv n 0 "no sidewalk on this section"
need_kv survived 1 "driver completed"

# An undefined road is an error for the count query, and the id query says so
# with a status rather than with an id. Only the SIGN of the count query's
# failure is asserted: the requirements promise that an undefined road id is
# reported as a failure, and the instruction's return-convention note says a
# failure is negative without fixing which negative value, so a build that
# answers -2 is as correct as one that answers -1.
run_driver rm_lane_by_type "$E6" 0 100.0 -1
need_neg bad_road_n "count for an undefined road id"
need_neg bad_road_id_rc "id query for an undefined road id"
need_kv bad_road_untouched 1 "out parameter for an undefined road id"
exit 0
