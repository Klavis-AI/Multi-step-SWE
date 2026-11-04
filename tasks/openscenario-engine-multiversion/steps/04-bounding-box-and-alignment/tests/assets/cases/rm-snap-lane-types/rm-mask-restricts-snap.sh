#!/usr/bin/env bash
# RM_SetSnapLaneTypes restricts which lane types a position handle is allowed
# to resolve a world coordinate onto. The anchor point sits at the centre of
# a BORDER lane (lane -2 of straight_500m.xodr's road 1). A handle with the
# mask left wide open resolves back onto that same border lane; a handle
# restricted to driving lanes only is pushed onto the nearest driving lane
# instead, at a non-zero lane offset, even though the world point handed in
# is identical in both runs.
source /tests/caselib.sh

# The snap-mask setters report success with a NON-NEGATIVE code, not with a
# particular one: the step's own Notes bullet tells the reader that road-manager
# position setters return 1 and 2 as well as 0 and that a correct solution must
# never be tested for `== 0`. Assert the sign here, exactly as
# rm-lane-geometry/*.sh already does for RM_SetLanePosition.
need_nonneg() {  # need_nonneg <key> <what>
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$2: driver printed no '$1=' line"
    [ "$got" -ge 0 ] 2>/dev/null || fail "$2: $1 expected a non-negative value, got '$got'"
}

run_driver rm_snap "$FIXTURES/straight_500m.xodr" 1 -2 250 -1
need_status 0 $? "rm_snap, wide-open mask"
need_kv init 0            "RM_Init"
need_kv anchor_road 1      "anchor road"
need_kv anchor_lane -2      "the anchor sits on a border lane"
need_close anchor_s 250.0 0.001  "anchor s"
need_nonneg snap_rc                "RM_SetSnapLaneTypes accepted the wide-open mask"
need_kv set_world 1              "RM_SetWorldXYHPosition succeeded"
need_kv road 1                     "still road 1"
need_kv lane -2                      "an unrestricted mask keeps the point on its own border lane"
need_close lane_offset 0.0 0.01        "no lateral correction was needed"
need_neg bad_handle_rc                   "a handle that was never created is rejected, not silently accepted"
need_kv survived 1                         "driver ran to the end"

# Same anchor, same world point, mask restricted to driving lanes only
# (LANE_TYPE_DRIVING = 2).
run_driver rm_snap "$FIXTURES/straight_500m.xodr" 1 -2 250 2
need_status 0 $? "rm_snap, driving-only mask"
need_kv anchor_lane -2   "the anchor is still on the border lane"
need_nonneg snap_rc            "RM_SetSnapLaneTypes accepted the driving-only mask"
need_kv lane -1              "a driving-only mask pushes the same world point onto the nearest driving lane"
need_close lane_offset -2.375 0.01  "the point is off-centre in its new lane, proving it was actually re-resolved"
