#!/usr/bin/env bash
source /tests/caselib.sh

# odrplot's command line: an optional output file name and an optional sampling
# step, both documented in its own usage text. The usage text misspells the
# program as "ordplot"; that typo is upstream's and is present at both ends of
# this step, so it is matched as-is rather than corrected.

run_tool "$ODRPLOT"
need_grep '^Usage: ordplot openDriveFile\.xodr' "$WORK/last.out" "odrplot usage line"
need_grep '\[Output file, default=output\.csv\]' "$WORK/last.out" "usage documents the output argument"
need_grep '\[Sampling_step, default=1\.0\]' "$WORK/last.out" "usage documents the sampling step"

# An explicit output name is honoured, and produces the same sampling as the
# default run does.
run_tool "$ODRPLOT" "$FIXTURES/e6mini.xodr" mine.csv
need_status 0 $? "odrplot with an explicit output name"
need_grep 'Created mine\.csv using stepsize 1\.00' "$WORK/last.out" "completion line names the chosen file"
need_file "$WORK/mine.csv"
pts=$(grep -c '^-\?[0-9]' "$WORK/mine.csv")
[ "$pts" -eq 21975 ] || fail "explicit output name changed the sampling: got $pts rows, expected 21975"

# A coarser sampling step is honoured: the lane inventory is unchanged but the
# geometry is sampled roughly five times less densely.
run_tool "$ODRPLOT" "$FIXTURES/e6mini.xodr" s5.csv 5.0
need_status 0 $? "odrplot with an explicit sampling step"
need_grep 'Created s5\.csv using stepsize 5\.00' "$WORK/last.out" "completion line echoes the sampling step"
need_file "$WORK/s5.csv"
lanes5=$(grep -c '^lane, ' "$WORK/s5.csv")
[ "$lanes5" -eq 15 ] || fail "sampling step changed the lane count: got $lanes5, expected 15"
pts5=$(grep -c '^-\?[0-9]' "$WORK/s5.csv")
[ "$pts5" -ge 4000 ] && [ "$pts5" -le 4800 ] \
    || fail "expected about 4395 sample rows at stepsize 5.0, got $pts5"
[ "$pts5" -lt "$pts" ] || fail "coarser sampling did not reduce the sample count"

# A file that is not an OpenDRIVE network is an error, and so is one that is not
# there at all. The exact wording is not pinned, only that neither succeeds.
printf 'not xodr\n' > "$WORK/bad.xodr"
run_tool "$ODRPLOT" "$WORK/bad.xodr"
[ "$?" -eq 0 ] && fail "odrplot reported success on a non-OpenDRIVE file"

run_tool "$ODRPLOT" "$WORK/no_such_file.xodr"
[ "$?" -eq 0 ] && fail "odrplot reported success on a missing file"
exit 0
