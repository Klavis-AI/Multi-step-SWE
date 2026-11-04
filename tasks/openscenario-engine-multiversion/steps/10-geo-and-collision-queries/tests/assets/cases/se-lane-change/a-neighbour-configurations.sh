#!/usr/bin/env bash
# SE_ObjectCanChangeLanes answers, for one object, whether a lane change to the
# left, to the right, to both or to neither is possible -- a query over the road
# network and the road marks on the lane boundaries, not over the scenario.
#
# tri_lane_cars.xosc parks one car on each of the four configurations
# tri_lane_600m.xodr provides, so a single run asks the same question four times
# and must give four different answers. A build that returns a constant, or that
# ignores the drivable lanes neighbouring the entity, cannot produce this
# spread. Every code below was read off the running engine.
#
# Note for a later round: this fixture does NOT discriminate the road-mark half
# of the rule. The marks that govern the two moves it allows are
# `laneChange="both"` (tri_lane_600m.xodr:66 for lane -1's boundary, :76 for
# lane -2's), and every `laneChange="none"` mark sits on a boundary the engine
# denies for another reason — the centre lane, or a `border` neighbour. A build
# that counted only drivable neighbours, with no mark reading at all, would
# answer the same `0 1 2 3`. A fixture that separates the two would need a
# crossable and a non-crossable mark between two otherwise identical drivable
# neighbours.
source /tests/caselib.sh

cp "$FIXTURES/tri_lane_cars.xosc" "$FIXTURES/tri_lane_600m.xodr" "$WORK/"

run_driver se_lane_change "$WORK/tri_lane_cars.xosc" noreport
need_status 0 $? "se_lane_change"
need_kv init 0 "SE_Init"
need_kv nobj_obj 4 "the fixture's four entities"
need_kv survived 1 "SE_Close"

# lane -1: the centre line to its left is solid, lane -2 to its right is a
# driving lane behind a broken line.
need_grep '^obj0 lane=-1 canchange=2$' "$WORK/last.out" "outermost-left driving lane"
# lane -2: driving lanes behind broken lines on both sides.
need_grep '^obj1 lane=-2 canchange=3$' "$WORK/last.out" "middle driving lane"
# lane -3: driving lane behind a broken line on one side, a solid line and a
# non-driving border on the other.
need_grep '^obj2 lane=-3 canchange=1$' "$WORK/last.out" "outermost-right driving lane"
# lane 1: the only driving lane on its side of the centre line, solid marks on
# both boundaries.
need_grep '^obj3 lane=1 canchange=0$' "$WORK/last.out" "isolated driving lane"

# The four answers really are four different answers, so a constant cannot pass.
codes=$(sed -n 's/^obj[0-9] lane=-\?[0-9]* canchange=//p' "$WORK/last.out" | sort -u | tr '\n' ' ')
[ "$codes" = "0 1 2 3 " ] || fail "expected the four legality codes 0 1 2 3, got '$codes'"

# An unknown object is the documented error, and error is distinct from "no
# lane change possible": -1, not 0.
need_kv badobj -1 "unknown object id"
need_kv badobj_negative -1 "negative object id"
