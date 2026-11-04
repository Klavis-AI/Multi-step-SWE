#!/usr/bin/env bash
# SE_SetSnapLaneTypes is the scenario-engine-object counterpart of
# RM_SetSnapLaneTypes: it restricts which lane types an externally reported
# object may resolve onto. The same world point that sits on a border lane
# resolves there under a wide-open mask, and onto the nearest driving lane
# under a driving-only mask -- and a call with an id no scenario defines is
# rejected outright, both before SE_Init and after.
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

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_snap "$WORK/two_car_brake.xosc" 250 -3.910 -1
need_status 0 $? "se_snap, wide-open mask"
need_neg snap_before_init    "SE_SetSnapLaneTypes before SE_Init is rejected, not silently accepted"
need_kv init 0                 "SE_Init"
need_nonneg snap_rc                  "SE_SetSnapLaneTypes accepted the wide-open mask on the added object"
need_neg bad_id_rc                 "SE_SetSnapLaneTypes on an id no scenario defines is rejected"
need_kv report_rc 0                  "the position report succeeded"
need_kv road 1                         "the object lands on road 1"
need_kv lane -2                          "an unrestricted mask keeps the point on its own border lane"
need_close lane_offset 0.0 0.01            "no lateral correction was needed"
need_kv survived 1                           "driver ran to the end"

# Same world point, mask restricted to driving lanes only (LANE_TYPE_DRIVING = 2).
run_driver se_snap "$WORK/two_car_brake.xosc" 250 -3.910 2
need_status 0 $? "se_snap, driving-only mask"
need_nonneg snap_rc      "SE_SetSnapLaneTypes accepted the driving-only mask"
need_kv lane -1        "a driving-only mask pushes the same world point onto the nearest driving lane"
need_close lane_offset -2.375 0.01  "the point is off-centre in its new lane, proving it was actually re-resolved"
