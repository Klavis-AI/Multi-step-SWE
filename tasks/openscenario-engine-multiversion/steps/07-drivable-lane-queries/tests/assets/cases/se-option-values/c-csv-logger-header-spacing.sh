#!/usr/bin/env bash
# R07.11 - this step's own --csv_logger delta. The header loses the space that
# v2.46.4 prints BEFORE each comma; the single space AFTER each comma stays.
#
# This is a SCORED case, and it is the only check in the step that tells the two
# bounding tags apart on the header's spelling. It deliberately does NOT live in
# the csv-logger-header GATE group: a gate group has to pass at v2.46.4 as well,
# so an assertion pinning either spelling would fail one of the two tags by
# construction, and it would turn this step's own new behaviour into an unpriced
# veto instead of scored work.
#
# Subject mismatch acknowledged. The group ceiling (14) is reached, so there was
# no room for a csv-logger scored group of its own; se-option-values is the
# scored group nearest in subject - how a command-line option surfaces - and the
# placement is recorded in REVIEW_LOG.md rather than left to be rediscovered.
#
# Only the SPELLING is asserted here. The header's field count, its column names
# and the per-row shape are the gate group's subject and are not repeated.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$WORK/two_car_brake.xosc"
cp "$FIXTURES/straight_500m.xodr" "$WORK/straight_500m.xodr"

# The scenario is named RELATIVE to $WORK, which is this case's own fresh CWD:
# the engine echoes the path it was handed into the preamble, so an absolute
# path would write the per-run work directory into the file being asserted.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 --csv_logger out.csv
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv" "csv logger output"

sed -n '7p' "$WORK/out.csv" > "$WORK/hdr"

# The three column names instruction.md spells out, contiguous, with no space
# before any comma and one space after each. v2.46.4 writes
# "Index [-] , TimeStamp [s] , #1 Entity_Name [-] , " and fails this.
need_grep '^Index \[-\], TimeStamp \[s\], #1 Entity_Name \[-\], ' "$WORK/hdr" \
    "header spelled without the space before each comma"

# ...and nowhere else in the header either, so a build that tidied only the
# leading columns does not pass. Measured: 0 occurrences at v2.51.1, 62 at
# v2.46.4.
need_not_grep ' ,' "$WORK/hdr" "no header column keeps a space before its comma"
exit 0
