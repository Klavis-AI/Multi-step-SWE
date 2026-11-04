#!/usr/bin/env bash
source /tests/caselib.sh

# The --csv option overrides the output filename and writes to cwd.

run_esmini --osc "$FIXTURES/two_car_brake.xosc" \
    --headless --fixed_timestep 0.05 --record sim --disable_stdout
need_status 0 $? "run_esmini"
need_file "$WORK/sim.dat"

mkdir -p "$WORK/sub"
mv "$WORK/sim.dat" "$WORK/sub/"

run_tool "$DAT2CSV" --csv other.csv --file "$WORK/sub/sim.dat"
need_status 0 $? "dat2csv with --csv"
need_file "$WORK/other.csv"
[ ! -e "$WORK/sub/sim.csv" ] || fail "default csv should not exist"

# The option table calls --print_csv "write the CSV to stdout instead of to a
# file". The second half of that sentence is the part a naive implementation
# drops: printing the rows AND still writing the file passes any stdout-only
# check. So assert both halves.
rm -f "$WORK/other.csv"
run_tool "$DAT2CSV" --print_csv --file "$WORK/sub/sim.dat"
need_status 0 $? "dat2csv --print_csv"
need_grep '^time, id, name,' "$WORK/last.out" "--print_csv puts the header on stdout"
# header + 2 objects x 202 steps
pl=$(wc -l < "$WORK/last.out")
[ "$pl" -eq 405 ] || fail "--print_csv wrote $pl lines to stdout, expected 405"
[ ! -e "$WORK/sub/sim.csv" ] || fail "--print_csv also wrote the default csv next to the input"
[ ! -e "$WORK/other.csv" ] || fail "--print_csv also wrote a csv in the working directory"

# ...and --csv does not change that: the stdout form wins over the named target.
run_tool "$DAT2CSV" --print_csv --csv also.csv --file "$WORK/sub/sim.dat"
need_status 0 $? "dat2csv --print_csv --csv"
need_grep '^time, id, name,' "$WORK/last.out" "--print_csv still prints when --csv is given"
[ ! -e "$WORK/also.csv" ] || fail "--print_csv wrote also.csv instead of printing"

# An output path that cannot be created is a reported failure, not a silent one
# and not a directory the tool invents for itself.
run_tool "$DAT2CSV" --csv "$WORK/absent_dir/out.csv" --file "$WORK/sub/sim.dat"
st=$?
[ "$st" -ne 0 ] || fail "dat2csv reported success writing into a directory that does not exist"
need_file "$WORK/last.out"
need_grep 'absent_dir/out.csv' "$WORK/last.out" "the unwritable path is named in the diagnostic"
[ ! -e "$WORK/absent_dir" ] || fail "dat2csv created the missing directory instead of reporting"
[ ! -e "$WORK/sub/sim.csv" ] || fail "dat2csv fell back to the default path after --csv failed"
