#!/usr/bin/env bash
# The CSV logger's column set changed: it now carries the bounding box of each
# entity and its lane identity, and the rows carry real values for them.
#
# The preamble and the six fixed lines before the header are unchanged, and
# the header still misspells "Entity" and still puts a space before every
# comma. Only the middle of the entity block moved.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --csv_logger "$WORK/out.csv"
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv"

# The preamble is untouched. The three GIT lines are filled in from `git` at
# configure time and fall back to `N/A` only when the source tree is not a
# checkout, so the VALUE is a property of the candidate's workspace and not of
# their solution -- a candidate who keeps /app under git gets a real 7-hex-digit
# revision here, plus a trailing `+` when the tree is dirty. Require the line
# and its shape, never a particular revision.
need_grep '^esmini GIT REV: ([0-9a-fA-F]{7}\+?|N/A)$' "$WORK/out.csv" "GIT REV preamble line"
need_grep '^esmini GIT TAG: .' "$WORK/out.csv" "GIT TAG preamble line"
need_grep '^esmini GIT BRANCH: .' "$WORK/out.csv" "GIT BRANCH preamble line"
need_grep '^esmini BUILD VERSION: .' "$WORK/out.csv" "BUILD VERSION preamble line"
# The scenario name is the run's own --osc argument echoed back, so the line is
# matched by its file name and not by the spelling of the path in front of it: a
# bare name when one was given, a path when a path was given. No agent-visible
# document narrows the value to the basename -- step 10's instruction writes
# `Scenario File Name: <the .xosc given to --osc>` and step 02's
# WORKSPACE_HANDOFF `<the file as it was named on the command line>` -- and the
# run above names the scenario by its absolute path, so pinning the bare name
# here would fail a solution that follows the documents. The steps that read the
# same field later match it the same way (step 06 cli-formats, step 08
# csv-logger-shape).
need_grep '^Scenario File Name: .*two_car_brake\.xosc$' "$WORK/out.csv" "scenario name line"
need_grep '^Number of Vehicles: 2$' "$WORK/out.csv" "vehicle count line"

# The header keeps upstream's `Entitity_` misspelling of "Entity" and the space
# before every comma outside the two lane columns -- the column layout around
# the new box columns is unchanged, not merely similar.
need_grep '^Index \[-\] , TimeStamp \[s\] , #1 Entitity_Name \[-\] , #1 Entitity_ID \[-\] ,' \
    "$WORK/out.csv" "the header opens with the unchanged, still-misspelled entity fields"
need_grep '#2 Entitity_Name \[-\] , #2 Entitity_ID \[-\] ,' \
    "$WORK/out.csv" "the second entity block keeps the same spelling"

# Each entity block now names a bounding box and a lane.
for e in 1 2; do
    for col in bb_x bb_y bb_z bb_length bb_width bb_height lane_id; do
        need_grep "#$e $col" "$WORK/out.csv" "entity $e column $col in the header"
    done
done

# The box columns sit together, between Wheel_Rotation and World_Position_X,
# in the order center (x, y, z) then dimensions (length, width, height).
hdr=$(grep -m1 '^Index \[-\] , TimeStamp' "$WORK/out.csv")
[ -n "$hdr" ] || fail "no CSV header line found"
i_rot=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 Wheel_Rotation \[-\] $' | cut -d: -f1)
i_bbx=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 bb_x \[m\] $' | cut -d: -f1)
i_bbl=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 bb_length \[m\] $' | cut -d: -f1)
i_wpx=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 World_Position_X \[m\] $' | cut -d: -f1)
[ -n "$i_rot" ] || fail "Wheel_Rotation column missing"
[ -n "$i_bbx" ] || fail "bb_x column missing"
[ -n "$i_bbl" ] || fail "bb_length column missing"
[ -n "$i_wpx" ] || fail "World_Position_X column missing"
[ "$i_bbx" -eq $((i_rot + 1)) ] || fail "bb_x is not the column right after Wheel_Rotation"
[ "$i_bbl" -eq $((i_bbx + 3)) ] || fail "bb_length is not three columns after bb_x"
[ "$i_wpx" -eq $((i_bbl + 3)) ] || fail "the six box columns are not contiguous before World_Position_X"

# The lane columns sit between Lateral_Distance_Lanem and World_Heading_Angle.
i_lat=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 Lateral_Distance_Lanem \[m\] $' | cut -d: -f1)
i_lid=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 lane_id$' | cut -d: -f1)
i_lof=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 lane_offset\[m\] $' | cut -d: -f1)
i_hea=$(printf '%s' "$hdr" | tr ',' '\n' | grep -n '^ #1 World_Heading_Angle \[rad\] $' | cut -d: -f1)
[ -n "$i_lat" ] && [ -n "$i_lid" ] && [ -n "$i_lof" ] && [ -n "$i_hea" ] \
    || fail "the lane columns are missing from the header"
[ "$i_lid" -eq $((i_lat + 1)) ] || fail "lane_id is not the column after Lateral_Distance_Lanem"
[ "$i_lof" -eq $((i_lat + 2)) ] || fail "lane_offset is not the column after lane_id"
[ "$i_hea" -eq $((i_lof + 1)) ] || fail "World_Heading_Angle does not follow the lane columns"

# The two lane columns are the one place the header's whitespace is not
# uniform, and upstream is not even self-consistent about it: `lane_id` never
# has a space before its comma, and the unit suffix of `lane_offset` is closed
# up in the #1 block and spaced out from #2 on. Both spellings are documented;
# assert the second block too, so a solution that emits one spelling everywhere
# is caught here rather than in a later step.
fields=$(printf '%s' "$hdr" | tr ',' '\n')
printf '%s\n' "$fields" | grep -q '^ #2 lane_id$' \
    || fail "the #2 block's lane_id field is missing or has a space before its comma"
printf '%s\n' "$fields" | grep -q '^ #2 lane_offset \[m\] $' \
    || fail "the #2 block spells lane_offset without the space before [m] that upstream emits from #2 on"

# And the data rows carry the entity's box and its lane, not blanks.
row=$(grep -m1 '^0, 0\.000000,' "$WORK/out.csv")
[ -n "$row" ] || fail "no first data row found"
printf '%s' "$row" | tr ',' '\n' | sed -n "${i_bbl}p" | grep -Eq '^ 5\.000000' \
    || fail "bb_length data value is not 5.0 on the first row"
printf '%s' "$row" | tr ',' '\n' | sed -n "${i_lid}p" | grep -Eq '^ -1$' \
    || fail "lane_id data value is not -1 on the first row"

# The values, not only the columns. instruction.md section 1 states that the
# three bb_ centre columns are the offset of the box centre from the entity's
# reference point and that all six numbers are the entity's own declared
# <BoundingBox>; both entities in this fixture declare centre (1.4, 0.0, 0.75)
# and dimensions (5.0, 2.0, 1.5), and both are placed with offset 0, i.e. on
# their lane's centreline. A logger writing a constant centre or a stale
# lateral offset would satisfy the presence checks above and fail these.
printf '%s' "$row" | tr ',' '\n' | sed -n "${i_bbx}p" | grep -Eq '^ 1\.400000$' \
    || fail "bb_x data value is not the declared centre offset 1.4 on the first row"
printf '%s' "$row" | tr ',' '\n' | sed -n "$((i_bbx + 1))p" | grep -Eq '^ 0\.000000$' \
    || fail "bb_y data value is not the declared centre offset 0.0 on the first row"
printf '%s' "$row" | tr ',' '\n' | sed -n "$((i_bbx + 2))p" | grep -Eq '^ 0\.750000$' \
    || fail "bb_z data value is not the declared centre offset 0.75 on the first row"
printf '%s' "$row" | tr ',' '\n' | sed -n "${i_lof}p" | grep -Eq '^ -?0\.000000$' \
    || fail "lane_offset is not 0 on the first row, where the entity sits on its lane centreline"
