#!/usr/bin/env bash
# The edges of --precision, all measured on the running tool.
#
#   --precision 0     valid: the floats print as integers, correctly rounded
#   --precision 25    out of range (above): a warning, and a fall back to three
#   --precision -1    out of range (below): the same warning and the same fall
#                     back, because the contract's range is 0 to 20 inclusive
#                     and everything outside it behaves the same way
#   --precision       value omitted: three decimals, as the help text says
#
# and the option is advertised in the tool's own help. None of these is an
# error exit: a precision the tool will not honour is a warning, not a refusal
# to convert.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_esmini --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05 \
           --record "$WORK/rec.dat"
need_status 0 $? "esmini with --record"
need_file "$WORK/rec.dat"

convert() {  # convert <args...>
    rm -f "$WORK/rec.csv"
    run_tool "$DAT2CSV" --file "$WORK/rec.dat" "$@"
}

# Zero decimals is a legal request, and the rounding is arithmetic: the Ego's
# lane centre at y = -1.535 becomes -2, not -1 and not -1.535.
convert --precision 0
need_status 0 $? "dat2csv --precision 0"
need_file "$WORK/rec.csv"
need_grep '^time, id, name, x, y, z, h, p, r, speed, wheel_angle, wheel_rot$' \
    "$WORK/rec.csv" "column header"
need_grep '^0, 0, Ego, 20, -2, 0, 0, 0, 0, 20, 0, 0$' "$WORK/rec.csv" "no decimals at all"
# No fractional part ANYWHERE in the file, in any column and at any offset:
# a decimal point followed by a digit must not occur. The pattern is
# deliberately unanchored -- an anchored one would only inspect the first
# column and would let a build that zeroed the index while leaving x, y or
# speed at the default three decimals through.
need_not_grep '[0-9]+\.[0-9]' "$WORK/rec.csv" "no fractional part in any column at precision 0"

# An out-of-range precision is reported and the default is used instead. Both
# ends of "out of range" are checked: 25 is above the supported range and -1 is
# below it, and requirement 5 states the same outcome for either -- it names a
# negative value in as many words.
for n in 25 -1; do
    convert --precision $n
    need_status 0 $? "dat2csv --precision $n still converts"
    need_grep 'Warning: Invalid precision\. Using default value of 3\.' "$WORK/last.out" \
        "--precision $n is reported"
    need_file "$WORK/rec.csv"
    need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, ' "$WORK/rec.csv" \
        "--precision $n fell back to three decimals"
done

# The value is optional; omitting it selects the same default, quietly.
convert --precision
need_status 0 $? "dat2csv --precision with the value omitted"
need_file "$WORK/rec.csv"
need_grep '^0\.000, 0, Ego, 20\.000, -1\.535, ' "$WORK/rec.csv" "omitted value means three decimals"
need_not_grep 'Unrecognized argument' "$WORK/last.out" "--precision is a recognised option"

# And it is documented in the tool's own help. dat2csv exits non-zero from
# --help at both ends of this step, so the text is what is asserted, not the
# status.
run_tool "$DAT2CSV" --help
need_grep '^Usage: dat2csv \[options\]' "$WORK/last.out" "dat2csv help banner"
need_grep '^  --precision \[number of decimals\]' "$WORK/last.out" "--precision is listed in the help"
need_grep 'Output floating point number precision' "$WORK/last.out" "--precision help text"
