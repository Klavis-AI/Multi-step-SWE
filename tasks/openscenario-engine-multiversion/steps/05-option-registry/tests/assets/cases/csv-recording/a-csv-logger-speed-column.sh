#!/usr/bin/env bash
# Inherited. `esmini --csv_logger <filename> ...` must still produce a CSV
# file with one row per simulation step, reporting entity state.
#
# The "Entity" column name is spelled "Entitity" before this step and "Entity"
# from this step onward, and total line count also drifts by a line inside
# this step's range -- neither is part of the inherited contract, so this case
# locates a column that is spelled identically at both ends (by NAME, via
# csv_column, never by position) and checks only that column's value.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --csv_logger out.csv
need_status 0 $? "esmini --csv_logger <filename>"
need_file "$WORK/out.csv" "csv logger output"

# R05.50: the run preamble carries the build's version identity as the
# `esmini GIT TAG:` and `esmini BUILD VERSION:` lines (lines 2 and 4 of the
# six-line preamble). The VALUE is a property of the candidate's workspace -
# taken from `git describe` at configure time, falling back to `N/A` when the
# tree is not a checkout - so only the line and its shape are asserted, never a
# particular tag. Both lines are asserted here because this is the only group
# that reads the CSV preamble and R05.50 names both.
sed -n '2p' "$WORK/out.csv" > "$WORK/pl2"
sed -n '4p' "$WORK/out.csv" > "$WORK/pl4"
need_grep '^esmini GIT TAG: [^ ]' "$WORK/pl2" "GIT TAG preamble line"
need_grep '^esmini BUILD VERSION: [^ ]' "$WORK/pl4" "BUILD VERSION preamble line"

sed -n '7p' "$WORK/out.csv" > "$WORK/hdr"
col=$(csv_column "#1 Current_Speed [m/s]" "$WORK/hdr")
[ -n "$col" ] || fail "Current_Speed column not found in csv header"

sed -n '8p' "$WORK/out.csv" > "$WORK/first"
val=$(awk -F, -v c="$col" '{gsub(/^[ \t]+|[ \t]+$/,"",$c); print $c}' "$WORK/first")
[ "$val" = "10.000000" ] || fail "expected first-row Current_Speed 10.000000, got $val"
exit 0
