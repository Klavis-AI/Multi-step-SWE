#!/usr/bin/env bash
# dat2csv gains a --precision option: the number of decimals it writes for every
# floating-point field of a decoded recording. Without it the tool keeps its
# historical three.
#
# The recording is made by the candidate's own esmini and decoded by the
# candidate's own dat2csv, so this asserts the pair, not a fixture.
#
# Argument order matters and is not cosmetic: at the target release
# `dat2csv --precision 6 rec.dat` aborts out of std::stoi trying to read "rec.dat"
# as the precision, because --precision takes an OPTIONAL value. The file has to
# be named with --file, or before the flag.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/rec.dat"
need_status 0 $? "esmini with --record"
need_file "$WORK/rec.dat"

# decimals_of <csv> <1-based column> -> the sorted set of decimal-digit counts
# seen in that column over every data row, as a space-separated list.
decimals_of() {
    awk -F', *' -v c="$2" '/^[0-9]/ {
        n = $c
        if (n !~ /\./) { print 0; next }
        sub(/^[^.]*\./, "", n)
        print length(n)
    }' "$1" | sort -nu | tr '\n' ' '
}

convert() {  # convert <args...> -- fresh output every time
    rm -f "$WORK/rec.csv"
    run_tool "$DAT2CSV" --file "$WORK/rec.dat" "$@"
}

# Historical default: three decimals, and the column header is untouched.
convert
need_status 0 $? "dat2csv with no precision option"
need_file "$WORK/rec.csv"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/rec.csv" "column header"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, ' "$WORK/rec.csv" "default three decimals"
got=$(decimals_of "$WORK/rec.csv" 1)
[ "$got" = "3 " ] || fail "default precision: expected only 3-decimal times, got '$got'"

# --precision 1 is the bottom of the supported range above zero, and it is the
# one value where a single decimal is all the tool may write: the Ego's lane
# centre at y = -1.535 prints as -1.5, and every float column has exactly one
# decimal digit. 1 is named in the contract alongside 0, 6 and 10.
convert --precision 1
need_status 0 $? "dat2csv --precision 1"
need_file "$WORK/rec.csv"
need_grep '^0\.0, 0, Ego, 20\.0, -1\.5, ' "$WORK/rec.csv" "one decimal, first row"
# The whole row, not just its leading columns: every floating-point field --
# including z, h, p, r, speed, wheel_angle and wheel_rot -- carries the single
# decimal the option asked for and is not left at the default three.
need_grep '^0\.0, 0, Ego, 20\.0, -1\.5, 0\.0, 0\.0, 0\.0, 0\.0, 20\.0, 0\.0, 0\.0$' \
    "$WORK/rec.csv" "one decimal in every float column"
for col in 1 4 5 10; do
    got=$(decimals_of "$WORK/rec.csv" "$col")
    [ "$got" = "1 " ] || fail "--precision 1: column $col has decimal counts '$got'"
done

# --precision 6 widens every float field, not just one of them.
convert --precision 6
need_status 0 $? "dat2csv --precision 6"
need_file "$WORK/rec.csv"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/rec.csv" "column header is unchanged by --precision"
need_grep '^0\.000000, 0, Ego, 20\.000000, -1\.535000, ' "$WORK/rec.csv" "six decimals, first row"
need_grep '^0\.050000, 0, Ego, 21\.000000, .*, 2\.857143$' "$WORK/rec.csv" "six decimals, second sample"
for col in 1 4 5 10; do
    got=$(decimals_of "$WORK/rec.csv" "$col")
    [ "$got" = "6 " ] || fail "--precision 6: column $col has decimal counts '$got'"
done

# --precision 10 goes wider still, and the extra digits are real: one third of
# a wheel rotation prints as 2.8571428571, not as 2.8570000000.
convert --precision 10
need_status 0 $? "dat2csv --precision 10"
need_file "$WORK/rec.csv"
got=$(decimals_of "$WORK/rec.csv" 1)
[ "$got" = "10 " ] || fail "--precision 10: expected only 10-decimal times, got '$got'"
need_grep '^0\.0500000000, 0, Ego, 21\.0000000000, .*, 2\.8571428571$' "$WORK/rec.csv" \
    "ten decimals carry real digits"

# The decoded content is otherwise the same recording: two entities, 10 s at
# 0.05 s, same last sample.
rows=$(grep -c '^[0-9]' "$WORK/rec.csv")
[ "$rows" -ge 400 ] && [ "$rows" -le 410 ] || fail "expected about 404 sample rows, got $rows"
