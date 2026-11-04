#!/usr/bin/env bash
# Gate (proof of life). The CSV logger's row count and header shape must be unaffected by
# this step.
#
# The header row's SPACING also changes inside this step's range ("Index [-] ,"
# at v2.46.4 becomes "Index [-]," at v2.51.1), and this case does NOT assert it -
# see the note at the regexes below. The case is scoped to what instruction.md's
# "--csv_logger table" paragraph states and to what is structural: the header's
# field count, the column names, and that the body is one well-formed row per
# frame with no field-count drift. All of those are identical at both bounding
# tags, which is what a gate case needs.
#
# Deliberately NOT asserted: the text of the preamble lines, and the values in
# the data rows. A correct implementation is free to reword a diagnostic or
# retune a float's formatting; pinning those would fail a solution that changed
# nothing this step grades, which is the opposite of what a gate group is
# for.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$WORK/two_car_brake.xosc"
cp "$FIXTURES/straight_500m.xodr" "$WORK/straight_500m.xodr"

# The engine echoes the scenario path it was handed into the preamble, so the
# scenario is named RELATIVE to $WORK (which is this case's own fresh CWD).
# Passing an absolute path would put the per-run work directory into the
# asserted output and make this case nondeterministic.
run_esmini --osc two_car_brake.xosc --headless --fixed_timestep 0.05 --csv_logger out.csv
need_status 0 $? "esmini with --csv_logger"
need_file "$WORK/out.csv" "csv logger output"

# Six preamble lines, then the header, then one row per frame. The scenario runs
# 10.05 s at a 0.05 s step, so there is a frame at 0.000 and one at 10.050.
lines=$(wc -l < "$WORK/out.csv")
[ "$lines" -eq 209 ] || fail "expected 209 lines in the csv log, got $lines"

sed -n '7p' "$WORK/out.csv" > "$WORK/hdr"
nf=$(awk -F',' '{print NF}' "$WORK/hdr")
[ "$nf" -eq 65 ] || fail "expected 65 header fields, got $nf"

# Four column names from the header instruction.md describes. The regexes are
# anchored on the name and the closing bracket so a field that merely contains
# the words cannot satisfy them, and the ' *,' tolerates a space before the
# comma that follows.
#
# That tolerance is deliberate and it is the whole reason this group survives.
# This step is exactly where the pre-comma space is removed, and this is a GATE
# group: it has to pass at BOTH bounding tags, on a tree that has done nothing
# yet as well as on a finished one. An assertion that pinned either spelling
# would fail one of the two tags by construction. So the spacing is not scored
# HERE. It IS scored elsewhere in this step: cases/se-option-values/
# c-csv-logger-header-spacing.sh is a SCORED case pinning the new spelling, and it
# fails at v2.46.4 by construction, which is what makes frozen row R07.11 graded.
# Do not add a spacing assertion to this group - it would break the gate at
# v2.46.4 and turn this step's own new behaviour into an unpriced veto.
need_grep 'Index \[-\] *,' "$WORK/hdr" "index column"
need_grep 'TimeStamp \[s\] *,' "$WORK/hdr" "timestamp column"
need_grep '#1 Entity_Name \[-\] *,' "$WORK/hdr" "first entity name column"
need_grep '#2 Entity_Name \[-\] *,' "$WORK/hdr" "second entity name column"

# One well-formed data row per frame: the body is 209 - 7 = 202 rows, and every
# one of them carries the header's field count. This catches a logger that drops
# a column, pads a row, or stops early, without asserting any value.
sed -n '8,$p' "$WORK/out.csv" > "$WORK/body"
nrows=$(grep -c . "$WORK/body")
[ "$nrows" -eq 202 ] || fail "expected 202 data rows, got $nrows"
bad=$(awk -F',' -v want="$nf" 'NF != want {n++} END {print n+0}' "$WORK/body")
[ "$bad" -eq 0 ] || fail "$bad data rows do not carry $nf fields"
exit 0
