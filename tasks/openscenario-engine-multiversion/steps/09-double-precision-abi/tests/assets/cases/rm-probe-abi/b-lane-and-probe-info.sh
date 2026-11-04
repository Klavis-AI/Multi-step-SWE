#!/usr/bin/env bash
# NEW. RM_GetLaneInfo and RM_GetProbeInfo take a double lookahead distance and
# fill structures whose members are double. The lookahead is where the widening
# shows: it is added to s, so any float in the path perturbs the result at
# roughly 1e-5 on these magnitudes.
source /tests/caselib.sh

run_driver rm_probe "$FIXTURES/straight_500m.xodr" 1 -1 123.456789 25.0 0.3
need_status 0 $? "rm_probe"

need_kv laneinfo 1 "RM_GetLaneInfo"
# s = 123.456789 plus a 25 m lookahead.
need_close li_s 148.456789 1e-12 "lane-info s at a 25 m lookahead"
need_close li_width 3.07 1e-12 "lane width at the lookahead point"
need_close li_curv 0 1e-15 "curvature on a straight road"

need_kv probe 1 "RM_GetProbeInfo"
need_close probe_s 148.456789 1e-12 "probe s agrees with lane-info s"
# The probe point is expressed in the position's local frame, which is rotated
# by the heading set earlier, so the local x is the lookahead foreshortened by
# cos(0.3) rather than the lookahead itself. Measured, not derived.
need_close probe_local_x 23.883412228140163 1e-9 "probe point in the rotated local frame"
need_close probe_rel_h -0.3 1e-12 "probe heading relative to the position's own heading"

# Two handles set to the same place differ by nothing.
need_kv diff_rc 0 "RM_SubtractAFromB"
need_close diff_ds 0 1e-15 "two coincident positions are zero apart"

need_kv survived 1 "driver reached the end without aborting"
