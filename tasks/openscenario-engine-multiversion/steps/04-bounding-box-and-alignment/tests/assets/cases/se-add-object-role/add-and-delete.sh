#!/usr/bin/env bash
# MERGED CASE. This file is two cases that used to live side by side in this
# group, returned-id.sh and delete-added.sh, folded into one file to stay
# inside the package's per-step case budget. Nothing was dropped: every
# assertion and every comment from both halves is below, in its original
# order. Each half still makes its own driver run, so each half still reads a
# freshly produced last.out exactly as it did before.
# test.sh.in scores the GROUP, not the case file: `score` is the share-weighted
# fraction of the SCORED groups that passed, and the case count never enters
# that arithmetic -- so merging two files into one changes no weight and no
# denominator. What it does cost is localisation. A failure in either half now
# fails the whole file and therefore the whole group, and groups.txt's count
# column counts .sh files on disk, so this file is ONE counted case carrying
# the assertions of two.
#
# ---------------------------------------------------------------------------
# Half 1 (was returned-id.sh)
# ---------------------------------------------------------------------------
# SE_AddObject returns the id of the object it created.
#
# The returned value is the next free id, not a fixed 0: every id the caller
# gets back is a live id that SE_GetObjectState and SE_GetObjectName accept,
# and two successive additions get two different ids -- neither of which
# collides with the ids the scenario's own entities already occupy.
#
# The object count is read three times -- before any addition, after the
# first, and after the second -- so a stub that returns one constant id, or
# one that hands out ids without actually registering the object, cannot pass.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_addobj "$WORK/two_car_brake.xosc" 0 0

# Before anything else: the driver calls SE_AddObject once BEFORE SE_Init, and
# that call must be refused. The driver prints that return value first, so the
# check belongs first. It is graded with need_neg -- the SIGN only -- because
# the instruction states that the refusal is negative without fixing which
# negative value it is, and a case may not pin what the contract does not say.
need_neg add_before_init "SE_AddObject before SE_Init is refused, not silently accepted"

need_kv init 0 "SE_Init"
need_kv nobj_0 2 "the scenario declares two entities"
need_kv id_a 2 "first added object takes the next free id"
need_kv nobj_1 3 "the first addition is counted"
need_kv id_b 3 "second added object takes the id after that"
need_kv nobj_2 4 "the second addition is counted"
need_kv ids_distinct 1 "the two ids are distinct and both are new"
need_kv id_a_state 2 "the state reports back the id that was returned"
need_kv id_b_state 3 "the second state reports back the second id"
need_kv state_a_rc 0 "the first object is readable"
need_kv state_b_rc 0 "the second object is readable"

# The type and category arguments given to SE_AddObject are not merely accepted
# and discarded: they are stored on the object and read back through
# SE_ScenarioObjectState. The driver passes 1 (VEHICLE) and 1 (CAR) for both
# additions, so both must read back as 1. Without this, an implementation that
# ignores arguments 2 and 3 entirely -- registering every added object as
# whatever its default is -- passes every other assertion in this half.
need_kv type_a 1 "the first object keeps the VEHICLE object_type it was added with"
need_kv category_a 1 "the first object keeps the CAR object_category it was added with"
need_kv type_b 1 "the second object keeps the VEHICLE object_type it was added with"
need_kv category_b 1 "the second object keeps the CAR object_category it was added with"
need_kv name_a "added_a" "the first object carries the name it was given"
need_kv name_b "added_b" "the second object carries the name it was given"
need_kv survived 1 "driver ran to the end"

# ---------------------------------------------------------------------------
# Half 2 (was delete-added.sh)
# ---------------------------------------------------------------------------
# An added object can be removed again, and removing it is what the count
# says it is: back by exactly one, leaving the other added object in place.
#
# The two ids come from the same run, so this case only means anything if
# SE_AddObject really hands back live ids rather than a placeholder.

run_driver se_addobj "$WORK/two_car_brake.xosc" 0 0
need_kv ids_distinct 1 "two live ids from two additions"
need_kv nobj_2 4 "two scenario entities plus two added objects"

# The delete succeeds, the count falls by one, and the object that was not
# deleted is still readable.
need_kv delete_rc 0 "SE_DeleteObject on an object that exists"
need_kv nobj_3 3 "one object fewer after the delete"
need_kv state_a_rc_after 0 "the other added object is still readable"

# A second delete of the same id is refused, because the id is gone.
need_neg delete_again_rc "SE_DeleteObject on an id that no longer exists"
need_kv survived 1 "driver ran to the end"
