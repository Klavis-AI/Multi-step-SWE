#!/usr/bin/env bash
# The other half of instruction requirement 7: "an entity that *does* have a
# route still follows its route; this changes only what the engine does when
# there is nothing to follow."
#
# junction_routed_and_free.xosc puts two vehicles on the SAME incoming road
# (road 2 of fabriksgatan.xodr, the one whose junction end is a successor) at
# the same position and the same speed. They differ in exactly one respect:
#
#   Routed  carries an AssignRouteAction with a route to road 1.
#   Free    carries no Route of any kind.
#
# From road 2 the straight-ahead continuation is road 0 -- road 2 arrives at
# the junction at heading -79.4 degrees and connecting road 14 continues it at
# -79.6, while the road 1 connection turns about 90 degrees away. So the route
# this scenario declares is the road the heuristic does NOT pick, which is what
# makes the pair discriminating: a build that applied the straight-ahead choice
# to both entities would send both to road 0, and a build that let a declared
# route be overridden by the heuristic would do the same. Only a build that
# follows the route when there is one and goes straight when there is not
# produces the two different exits asserted below.
#
# The A/B is the whole point, so the assertions come in pairs: same incoming
# road, same hop count, different outgoing road, in the same run. The
# connecting road id is still not pinned -- that is network topology -- but it
# is asserted to differ between the two entities, which is the route taking
# effect stated as a comparison rather than as a constant.
#
# Every number below was read off the running engine at the target release.
source /tests/caselib.sh

cp "$FIXTURES/junction_routed_and_free.xosc" "$FIXTURES/fabriksgatan.xodr" "$WORK/"

run_driver se_junction "$WORK/junction_routed_and_free.xosc" 400 0.05
need_status 0 $? "se_junction on the routed scenario"
cp "$WORK/last.out" "$WORK/run.out"

need_kv init 0 "SE_Init on junction_routed_and_free"
need_kv objects 2 "both entities loaded"
need_kv survived 1 "SE_Close"

# Routed: the declared route wins over the straight-ahead heuristic.
need_kv obj0_final_road 1 "the routed entity leaves on the road its route names"
need_kv obj0_hops 3 "the routed entity crossed exactly one junction"

# Free: same road, same speed, no route -- so the heuristic decides, and it
# decides differently.
need_kv obj1_final_road 0 "the routeless entity goes straight ahead, as before"
need_kv obj1_hops 3 "the routeless entity crossed exactly one junction"

# Stated as a joint property as well, so neither assertion above can pass by
# both entities doing the same thing.
r0=$(sed -n 's/^obj0_final_road=//p' "$WORK/run.out" | tail -1)
r1=$(sed -n 's/^obj1_final_road=//p' "$WORK/run.out" | tail -1)
[ -n "$r0" ] && [ -n "$r1" ] || fail "the driver reported no final road for one of the entities"
[ "$r0" != "$r1" ] || fail "route had no effect: both entities left on road '$r0'"

# Both entered the junction from the same road and neither looped back out the
# way it came in: the paths share their incoming road and diverge after it.
p0=$(sed -n 's/^obj0_path=//p' "$WORK/run.out" | tail -1)
p1=$(sed -n 's/^obj1_path=//p' "$WORK/run.out" | tail -1)
[ "${p0%%>*}" = "2" ] || fail "the routed entity did not start on road 2: path '$p0'"
[ "${p1%%>*}" = "2" ] || fail "the routeless entity did not start on road 2: path '$p1'"
[ "$p0" != "$p1" ] || fail "the two entities took the same path through the junction: '$p0'"
