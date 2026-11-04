#!/usr/bin/env bash
source /tests/caselib.sh

# odrplot samples every lane of a network to track.csv. This output was measured
# byte-for-byte identical at both ends of this step (950308 bytes for
# e6mini.xodr), so the exact row counts below are stable obligations rather than
# incidental numbers: they hold before and after the change this step asks for.
#
# odrplot writes its default output relative to the working directory, and
# test.sh runs every case with $WORK as its working directory, so this reads the
# fixture without writing anything back into the fixture corpus.

run_tool "$ODRPLOT" "$FIXTURES/e6mini.xodr"
need_status 0 $? "odrplot on e6mini"
need_grep 'Created track\.csv using stepsize 1\.00' "$WORK/last.out" "odrplot completion line"
need_grep 'xodr\.py track\.csv' "$WORK/last.out" "plotting hint names the output file"
need_file "$WORK/track.csv"

# e6mini is a single road with a single lane section carrying lanes -7..7.
lanes=$(grep -c '^lane, ' "$WORK/track.csv")
[ "$lanes" -eq 15 ] || fail "expected 15 lane rows for e6mini, got $lanes"

need_grep '^lane, 0, 0, 7, no-driving$' "$WORK/track.csv" "outermost left lane row"
need_grep '^lane, 0, 0, -7, no-driving$' "$WORK/track.csv" "outermost right lane row"
need_grep '^lane, 0, 0, -2, driving$' "$WORK/track.csv" "a driving lane row"

# Every lane id in -7..7 appears exactly once, on road 0, lane section 0.
for id in 7 6 5 4 3 2 1 0 -1 -2 -3 -4 -5 -6 -7; do
    n=$(grep -c "^lane, 0, 0, $id, " "$WORK/track.csv")
    [ "$n" -eq 1 ] || fail "lane id $id appears $n times, expected exactly 1"
done

# Six of the fifteen lanes are driving lanes; the split is a property of the
# network, so a build that mis-classifies lane type changes it.
drv=$(grep -c '^lane, 0, 0, -\?[0-9]\+, driving$' "$WORK/track.csv")
[ "$drv" -eq 6 ] || fail "expected 6 driving lanes in e6mini, got $drv"

# One sample per metre per lane.
pts=$(grep -c '^-\?[0-9]' "$WORK/track.csv")
[ "$pts" -eq 21975 ] || fail "expected 21975 sample rows at stepsize 1.0, got $pts"
need_grep '^-?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+, -?[0-9]+\.[0-9]+$' \
    "$WORK/track.csv" "sample point row shape"
exit 0
