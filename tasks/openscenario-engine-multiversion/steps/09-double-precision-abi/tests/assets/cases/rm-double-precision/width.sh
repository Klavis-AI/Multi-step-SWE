#!/usr/bin/env bash
# RM_GetLaneWidth and RM_GetLaneWidthByRoadId take double* out-parameters, and
# RM_GetRoadLength returns double. The static assertions on the declared types
# fail at v2.59.0. At runtime this case checks two different things, and both
# are needed:
#
#   - on fabriksgatan, that the widths are the RIGHT value (3.5). 3.5 is
#     exactly representable in single precision, so no tolerance on it can tell
#     a double implementation from a float one - it is a value check only.
#   - on straight_500m, that they are the right value AT DOUBLE PRECISION. That
#     lane is 3.07 m wide, which is not a binary fraction: a float anywhere in
#     the pipeline moves it by ~6.7e-8, five orders of magnitude outside the
#     1e-12 asserted here. The same width read back through RM_GetLaneInfo must
#     agree to the last bit, which catches a narrowing on either path.
source /tests/caselib.sh

X=123.456789012345

run_driver rm_double "$FIXTURES/fabriksgatan.xodr" 0 -1 10.0 "$X" -1.5
need_status 0 $? "rm_double"
need_kv init 0 "RM_Init"
need_kv nroads 16 "road count"
need_kv survived 1 "RM_Close"

# Lane width queries through both entry points. Value check, not precision:
# fabriksgatan's 3.5 is exact in both widths.
need_kv wbr_rc 0 "RM_GetLaneWidthByRoadId"
need_close width_by_road 3.5 0.001 "width by road"
need_kv w_rc 0 "RM_GetLaneWidth"
need_close width_handle 3.5 0.001 "width by handle"

# World-position precision: the reported X must match the input exactly.
# The driver normalises each setter's return to 1 for any non-negative code.
# Position setters return 1 and 2 as well as 0 to mean success, so the sign is
# the contract and the exact code is not.
need_kv setxyh 1 "RM_SetWorldXYHPosition"
need_kv getpos 1 "RM_GetPositionData"
x_out=$(sed -n 's/^pos_x=//p' "$WORK/last.out")
[ -n "$x_out" ] || fail "driver printed no pos_x= line"
awk -v x="$x_out" -v e="$X" 'BEGIN { d = x - e; if (d < 0) d = -d; exit (d <= 1e-12) ? 0 : 1 }' \
    || fail "X precision: |$x_out - $X| exceeds 1e-12"

# Second run, on the fixture whose driving lane is 3.07 m wide. This is the
# precision half: a width that needs more than 24 mantissa bits.
run_driver rm_double "$FIXTURES/straight_500m.xodr" 1 -1 148.456789 250.0 -1.5
need_status 0 $? "rm_double on straight_500m"
need_kv init 0 "RM_Init on straight_500m"
need_kv nroads 1 "straight_500m.xodr has one road"
need_kv survived 1 "RM_Close on straight_500m"

need_kv wbr_rc 0 "RM_GetLaneWidthByRoadId on straight_500m"
need_close width_by_road 3.07 1e-12 "lane width by road id, at double precision"
need_kv w_rc 0 "RM_GetLaneWidth on straight_500m"
need_close width_handle 3.07 1e-12 "lane width by handle, at double precision"

# The ERROR half of the convention these two entry points follow: exactly 0 on
# success, a negative code on any error. The driver asks for a road id that is
# in neither fixture and for a position handle the road manager never issued.
# The documents promise the sign and not the magnitude, so that is all this
# asserts -- a solution answering -2 passes here, which is the intent. Measured
# at v3.0.4 on both fixtures: both answer -1, leave the out-parameter alone and
# do not abort the driver. (An out-of-range LANE id is deliberately not used:
# on an already positioned handle it answers 0, which the instruction's
# "a zero-width lane is a success" rule makes correct.)
need_neg wbr_bad_rc      "RM_GetLaneWidthByRoadId on a road id that does not exist"
need_neg w_bad_handle_rc "RM_GetLaneWidth on a handle that was never issued"

# One road model, two entry points, one double. RM_GetLaneInfo at zero
# lookahead reports the width at the position the handle is already on, so the
# two answers are the same number or the pipeline narrowed somewhere.
need_kv li_rc 1 "RM_GetLaneInfo"
li_w=$(sed -n 's/^li_width=//p' "$WORK/last.out")
wh=$(sed -n 's/^width_handle=//p' "$WORK/last.out")
[ -n "$li_w" ] || fail "driver printed no li_width= line"
awk -v a="$li_w" -v b="$wh" 'BEGIN { d = a - b; if (d < 0) d = -d; exit (d <= 1e-12) ? 0 : 1 }' \
    || fail "lane width disagrees between entry points: RM_GetLaneInfo $li_w vs RM_GetLaneWidth $wh"
