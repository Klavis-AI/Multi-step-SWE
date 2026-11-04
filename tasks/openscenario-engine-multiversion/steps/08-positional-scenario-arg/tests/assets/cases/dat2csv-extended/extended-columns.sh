#!/usr/bin/env bash
source /tests/caselib.sh

# --extended adds road-manager columns between r and speed, asserted by name.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" \
    --headless --fixed_timestep 0.05 --record sim --disable_stdout
need_status 0 $? "run_esmini"
need_file "$WORK/sim.dat"

run_tool "$DAT2CSV" --extended --file "$WORK/sim.dat"
need_status 0 $? "dat2csv --extended"
need_file "$WORK/sim.csv"

head -1 "$WORK/sim.csv" > "$WORK/hdr.txt"

for col in time id name x y z h p r roadId laneId offset t s speed wheel_angle wheel_rot; do
    [ -n "$(csv_column "$col" "$WORK/sim.csv")" ] || fail "column '$col' missing from extended header"
done

nedit=$(csv_column name "$WORK/sim.csv")
eid=$(csv_column id "$WORK/sim.csv")
[ "$nedit" -gt "$eid" ] || fail "name column should follow id"
es=$(csv_column s "$WORK/sim.csv")
espeed=$(csv_column speed "$WORK/sim.csv")
[ "$es" -lt "$espeed" ] || fail "s column should precede speed"

# Presence of the headers proves nothing about their contents: a stub that
# widens the header and pads every row with zeros passes everything above.
# The fixture road straight_500m.xodr is one road, id 1, a single <line/>
# geometry from (0,0) with hdg 0 and length 500, and lane -1 is 3.07 m wide.
# That pins every road-manager column to a value derivable from the file:
#   roadId  == 1        the only road in the network
#   laneId  == -1       neither car ever changes lane
#   offset  == 0        both cars track the lane centre
#   t       == -1.535   lane -1 centre is -width/2 of a 3.07 m lane
#   s       == x        arc length along a straight reference line laid on +x
#   t       == y        signed lateral offset from that same reference line
# The last two are the ones with teeth: they can only hold if dat2csv really
# asked the road manager to resolve each pose, so they separate a populated
# --extended from a padded one.
c_roadId=$(csv_column roadId "$WORK/sim.csv")
c_laneId=$(csv_column laneId "$WORK/sim.csv")
c_offset=$(csv_column offset "$WORK/sim.csv")
c_t=$(csv_column t "$WORK/sim.csv")
c_s=$(csv_column s "$WORK/sim.csv")
c_x=$(csv_column x "$WORK/sim.csv")
c_y=$(csv_column y "$WORK/sim.csv")

awk -F, -v road="$c_roadId" -v lane="$c_laneId" -v off="$c_offset" \
        -v ct="$c_t" -v cs="$c_s" -v cx="$c_x" -v cy="$c_y" '
    NR == 1 { next }
    {
        n++
        if ($road + 0 != 1)      { printf "row %d: roadId %s, want 1\n", NR, $road; bad = 1 }
        if ($lane + 0 != -1)     { printf "row %d: laneId %s, want -1\n", NR, $lane; bad = 1 }
        if (($off + 0) > 0.001 || ($off + 0) < -0.001) { printf "row %d: offset %s, want 0\n", NR, $off; bad = 1 }
        d = ($ct + 0) + 1.535; if (d > 0.001 || d < -0.001) { printf "row %d: t %s, want -1.535\n", NR, $ct; bad = 1 }
        d = ($cs + 0) - ($cx + 0); if (d > 0.001 || d < -0.001) { printf "row %d: s %s != x %s\n", NR, $cs, $cx; bad = 1 }
        d = ($ct + 0) - ($cy + 0); if (d > 0.001 || d < -0.001) { printf "row %d: t %s != y %s\n", NR, $ct, $cy; bad = 1 }
    }
    END {
        if (n == 0) { print "extended csv has no data rows"; bad = 1 }
        exit bad
    }' "$WORK/sim.csv" > "$WORK/roadcols.txt" 2>&1 \
    || fail "extended road columns wrong: $(head -3 "$WORK/roadcols.txt" | tr '\n' ';')"

# --extended must not cost rows: the same 404 samples the plain form carries
# (2 objects x 202 steps over 10.05 s at dt=0.05) are all still there.
erows=$(($(wc -l < "$WORK/sim.csv") - 1))
[ "$erows" -eq 404 ] || fail "extended csv has $erows data rows, expected 404"

# extended has more columns than plain
run_tool "$DAT2CSV" --file "$WORK/sim.dat"
need_status 0 $? "dat2csv plain"
head -1 "$WORK/sim.csv" > "$WORK/hdr_plain.txt"
npl=$(awk -F, '{print NF}' "$WORK/hdr_plain.txt")
next=$(awk -F, '{print NF}' "$WORK/hdr.txt")
[ "$next" -gt "$npl" ] || fail "extended ($next) should have more columns than plain ($npl)"
