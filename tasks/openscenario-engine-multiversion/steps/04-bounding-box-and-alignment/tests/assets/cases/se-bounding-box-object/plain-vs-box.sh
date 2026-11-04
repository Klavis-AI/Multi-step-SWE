#!/usr/bin/env bash
# The box form and the plain form coexist and differ.
#
# SE_AddObject keeps its four-field signature and adds an object whose box
# comes from elsewhere; an object added that way still reports a usable id and
# a readable state, and both objects are counted. A library that only ever
# returns one kind of object cannot satisfy the contrast asserted here: the
# box form must report the caller's box and the plain form must not.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_bbox "$WORK/two_car_brake.xosc" 5.0 2.0 1.5 1.4 0.0 0.75 0
need_kv init 0 "SE_Init"
need_kv nobj_before 2 "the scenario's own two entities"

# Adding is refused before a scenario exists, for both forms.
need_neg add_before_init "SE_AddObject before SE_Init is refused"

# Both objects got a real id, and they are different ids.
need_kv bb_add_ok 1 "the box form returned an id"
need_kv plain_state_rc 0 "the plain form's object is readable"
need_kv plain_name "plain_obj" "the name the plain object was given"

# The box form carries the caller's dimensions; the plain form does not.
#
# The three zero-box rows below are numeric comparisons, not string matches on
# the driver's print. instruction.md section 3 states the VALUE ("each read
# back as 0"), never a print format, so a case that pinned the literal text
# "0.0000" would be grading this driver's %.4f rendering rather than the
# library's answer. need_close is caselib's form for exactly that distinction.
need_close bb_length 5.0 0.001 "box form length is the caller's"
need_close bb_width 2.0 0.001 "box form width is the caller's"
need_close plain_length 0.0 1e-6 "plain form length is unset"
need_close plain_width 0.0 1e-6 "plain form width is unset"
need_close plain_height 0.0 1e-6 "plain form height is unset"

# Both added objects take part in the scenario.
need_kv nobj_after 4 "two scenario entities plus two added objects"
need_kv survived 1 "driver ran to the end"
