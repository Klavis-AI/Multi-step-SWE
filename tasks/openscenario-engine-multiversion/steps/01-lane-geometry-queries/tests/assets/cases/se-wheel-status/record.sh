#!/usr/bin/env bash
# report.sh only checks SE_ReportObjectWheelStatus's return codes, which a
# stub that discards its arguments satisfies just as well as a real
# implementation. This case proves the reported angle is actually kept: it
# drives a recording with object 0 told a fixed nonzero wheel angle every
# step and object 1 told nothing, then decodes the recording with dat2csv and
# requires the two entities' wheel_angle columns to disagree.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_wheel_record "$WORK/two_car_brake.xosc" || fail "se_wheel_record exited $?"
need_kv init 0 "SE_Init with recording enabled"
need_kv wheel_rc 0 "SE_ReportObjectWheelStatus for object 0"
need_kv survived 1 "driver ran to the end"

need_file "$WORK/two_car_brake.dat"
run_tool "$DAT2CSV" "$WORK/two_car_brake.dat"
need_status 0 $? "dat2csv"
need_file "$WORK/two_car_brake.csv"

# Resolve the two columns by NAME rather than by position, which is what
# caselib's csv_column exists for. dat2csv puts a `Version: ...` line ahead of
# the column header, and csv_column reads line 1 as the header, so it is given
# the file with that first line dropped; the field numbers it returns still
# index the original rows identically.
tail -n +2 "$WORK/two_car_brake.csv" > "$WORK/cols.csv"
ang=$(csv_column wheel_angle "$WORK/cols.csv")
rot=$(csv_column wheel_rot "$WORK/cols.csv")
[ -n "$ang" ] || fail "no wheel_angle column in the dat2csv output"
[ -n "$rot" ] || fail "no wheel_rot column in the dat2csv output"

# Object 0 (Ego) was told angle 0.35 and rotation 1.2 every step: the recorded
# samples have to carry both back out, not just the return code that reported
# them. instruction.md section 5 names both columns.
ego_ang=$(awk -F', ' -v c="$ang" 'NR>2 && $3=="Ego" { v=$c+0; if (v>0.34 && v<0.36) n++ } END{print n+0}' \
    "$WORK/two_car_brake.csv")
[ "$ego_ang" -ge 1 ] || fail "no Ego sample in the recording carries the reported wheel_angle 0.35"

ego_rot=$(awk -F', ' -v c="$rot" 'NR>2 && $3=="Ego" { v=$c+0; if (v>1.19 && v<1.21) n++ } END{print n+0}' \
    "$WORK/two_car_brake.csv")
[ "$ego_rot" -ge 2 ] || fail "Ego's wheel_rot does not hold the reported rotation 1.2 (matched $ego_rot samples)"

# Object 1 (Target) was never told anything: nothing this case does may leak
# Ego's reported angle onto it. Only the angle is checked for leakage --
# wheel_rot advances on its own as a vehicle drives, so Target's rotation
# sweeping past 1.2 at some sample is normal and is not evidence of a leak.
target_bad=$(awk -F', ' -v c="$ang" 'NR>2 && $3=="Target" { v=$c+0; if (v>0.34 && v<0.36) n++ } END{print n+0}' \
    "$WORK/two_car_brake.csv")
[ "$target_bad" -eq 0 ] || fail "Target's wheel_angle picked up Ego's reported value"
