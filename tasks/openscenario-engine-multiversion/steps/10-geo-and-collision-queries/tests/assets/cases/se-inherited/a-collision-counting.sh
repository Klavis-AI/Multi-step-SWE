#!/usr/bin/env bash
# Regression floor. The long-standing collision bookkeeping in the embedding
# ABI -- switch detection on, run a scenario, ask an entity how many objects it
# is currently overlapping and which ones -- behaves identically at both ends of
# this step.
#
# The driver runs the scenario forward, reads the collision count while the two
# entities are still apart, then force-relocates the second entity onto the
# first and steps once more, so the transition from "not colliding" to
# "colliding with that specific partner" is produced by the engine rather than
# assumed.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_collision_legacy "$WORK/two_car_brake.xosc" 40
need_status 0 $? "se_collision_legacy on two_car_brake"
need_kv init 0 "SE_Init"
need_kv ncoll_obj0_before 0 "no collision while the entities are apart"
need_kv ncoll_obj0_after 1 "exactly one collision once they overlap"
need_kv coll0_partner 1 "the reported partner is the other entity"
need_kv badobj_ncoll -1 "collision count for an out-of-range id"
need_kv badobj_coll -1 "collision partner for an out-of-range id"
need_kv survived 1 "SE_Close"
need_not_grep '^coll0_partner=none$' "$WORK/last.out" "a counted collision has a retrievable partner"
