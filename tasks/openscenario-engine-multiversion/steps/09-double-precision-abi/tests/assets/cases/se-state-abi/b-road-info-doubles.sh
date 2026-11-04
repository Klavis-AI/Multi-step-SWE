#!/usr/bin/env bash
# NEW. The mode-addressed pose report and the road-info lookup are on the same
# widened ABI: SE_ReportObjectPosMode takes doubles for every coordinate, and
# SE_RoadInfo's s / global position / curvature are doubles.
source /tests/caselib.sh

run_driver se_state "$FIXTURES/two_car_brake.xosc" 12.3456789 4.5678901 0.7853981633974483 0.05
need_status 0 $? "se_state"

need_kv posmode_rc 0 "SE_ReportObjectPosMode accepted an absolute-mode pose"
need_kv step2_rc 0   "SE_StepDT after the mode-addressed report"
need_kv get2_rc 0    "SE_GetObjectState after the mode-addressed report"

# Reporting the same x/y through the mode-addressed entry point must land in
# exactly the same place as the XYH entry point did, to full double precision.
# A solution that widened one of the two and not the other fails here.
need_close x2 12.3456789 1e-12 "mode-addressed report agrees with the XYH report in x"
need_close y2 4.5678901  1e-12 "mode-addressed report agrees with the XYH report in y"

need_kv roadinfo_rc 0 "SE_GetRoadInfoAtDistance"
# 12.3456789 along the road plus a 12.5 m lookahead. The fractional part is the
# whole point: it is preserved to 1e-12 only if s is a double end to end.
need_close ri_s 24.8456789 1e-12 "road-info s at a 12.5 m lookahead"
need_close ri_x 24.8456789 1e-12 "road-info global x on this straight road"
# straight_500m.xodr is straight, so curvature is exactly zero.
need_close ri_curv 0 1e-15 "curvature on a straight road"

need_kv survived 1 "driver reached SE_Close without aborting"
