#!/usr/bin/env bash
# The legality answer has to come from where the object actually is, not from a
# table built once at load time. The driver re-places object 0 on each of the
# three right-hand lane centres in turn with SE_ReportObjectPos, steps the
# engine, and re-asks -- so the same object id must produce all three answers in
# one run.
#
# The second half is the control: a network with a single driving lane per side
# has no legal lane change anywhere, which is the answer a stub that always
# reports "both" would get wrong.
source /tests/caselib.sh

cp "$FIXTURES/tri_lane_cars.xosc" "$FIXTURES/tri_lane_600m.xodr" \
   "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_lane_change "$WORK/tri_lane_cars.xosc" report
need_status 0 $? "se_lane_change report mode"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# One object, moved across the three right-hand driving lanes, in order.
moved=$(grep -c '^moved lane=' "$WORK/last.out")
[ "$moved" -eq 3 ] || fail "expected 3 re-placed samples, got $moved"
need_grep '^moved lane=-1 canchange=2$' "$WORK/last.out" "re-placed on lane -1"
need_grep '^moved lane=-2 canchange=3$' "$WORK/last.out" "re-placed on lane -2"
need_grep '^moved lane=-3 canchange=1$' "$WORK/last.out" "re-placed on lane -3"

# The engine placed it where it was told: the three reported y values are the
# three lane centres, so the answers above are answers about three different
# lanes and not the same lane three times.
lanes=$(sed -n 's/^moved lane=\(-\?[0-9]*\) .*/\1/p' "$WORK/last.out" | sort -u | tr '\n' ' ')
[ "$lanes" = "-1 -2 -3 " ] || fail "expected the object to visit lanes -1 -2 -3, got '$lanes'"

# Control: straight_500m.xodr has one drivable lane on each side of the centre
# line and borders beyond them, so neither entity may change lanes in either
# direction. Note that the network declares lane 0 itself as type "driving"
# behind a broken, crossable mark -- the answer is 0 anyway, because the centre
# lane is never a lane-change target. Counting drivable neighbours without that
# rule would answer 1 here.
run_driver se_lane_change "$WORK/two_car_brake.xosc" noreport
need_status 0 $? "se_lane_change on a single-lane network"
need_kv init 0 "SE_Init on two_car_brake"
need_kv nobj_obj 2 "the fixture's two entities"
need_grep '^obj0 lane=-1 canchange=0$' "$WORK/last.out" "no neighbour to change into (Ego)"
need_grep '^obj1 lane=-1 canchange=0$' "$WORK/last.out" "no neighbour to change into (Target)"
need_kv survived 1 "SE_Close on two_car_brake"
