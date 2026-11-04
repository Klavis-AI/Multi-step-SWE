#!/usr/bin/env bash
source /tests/caselib.sh

# The default CSV output at v2.59.0 has a header with 12 columns and no
# leading version line. All recorded frames should appear.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" \
    --headless --fixed_timestep 0.05 --record sim --disable_stdout
need_status 0 $? "run_esmini"
need_file "$WORK/sim.dat"

run_tool "$DAT2CSV" "$WORK/sim.dat"
need_status 0 $? "dat2csv positional"
need_file "$WORK/sim.csv"

head -1 "$WORK/sim.csv" > "$WORK/hdr.txt"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' "$WORK/hdr.txt" "12-column header"
need_not_grep '^Version:' "$WORK/sim.csv" "no version line in default output"

# instruction.md requirement 1: "a run of n entities stepped for k recorded
# frames yields n * k data rows". Here n = 2 (Ego, Target) and k = 202: the run
# covers 0.000 s to 10.050 s inclusive at dt = 0.05, which is 201 intervals and
# so 202 sampled instants. That is 404 data rows, plus the one header line.
# A floor of 400 would accept a converter that silently dropped frames, so the
# count is pinned exactly, and the per-object split is checked as well -- 404
# rows split 403/1 would satisfy a bare total.
lines=$(wc -l < "$WORK/sim.csv")
[ "$lines" -eq 405 ] || fail "expected 405 lines (header + 2 objects * 202 frames), got $lines"

c_id=$(csv_column id "$WORK/sim.csv")
c_time=$(csv_column time "$WORK/sim.csv")
awk -F, -v ci="$c_id" -v ct="$c_time" 'NR > 1 { n[$ci + 0]++; t[$ct + 0]++ }
    END {
        nid = 0
        for (k in n) { nid++; if (n[k] != 202) { printf "object %d has %d rows, want 202\n", k, n[k]; bad = 1 } }
        nt = 0
        for (k in t) nt++
        if (nid != 2)   { printf "%d distinct object ids, want 2\n", nid; bad = 1 }
        if (nt != 202)  { printf "%d distinct timestamps, want 202\n", nt; bad = 1 }
        exit bad
    }' "$WORK/sim.csv" > "$WORK/shape.txt" 2>&1 \
    || fail "csv row distribution wrong: $(tr '\n' ';' < "$WORK/shape.txt")"

# First and last sampled instants, so a converter cannot pass by emitting 202
# copies of one frame per object.
need_grep '^0\.000, 0, Ego,' "$WORK/sim.csv" "the run starts at t=0"
need_grep '^10\.050, 1, Target,' "$WORK/sim.csv" "the run ends at t=10.050"
