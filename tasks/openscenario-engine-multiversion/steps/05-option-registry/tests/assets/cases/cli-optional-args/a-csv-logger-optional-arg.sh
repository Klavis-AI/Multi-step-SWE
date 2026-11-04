#!/usr/bin/env bash
# New. --csv_logger must accept an OPTIONAL filename: the bare form uses the
# documented default and still runs to completion (older releases require an
# argument and fail to parse without one), while the explicit form honors the
# given name and does not also create the default file.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --csv_logger
need_status 0 $? "esmini --csv_logger (bare form)"
need_file "$WORK/log.csv" "default csv_logger output file"

# The CSV header must spell "Entity", not the older "Entitity".
need_grep 'Entity_Name' "$WORK/log.csv" "csv header spells Entity correctly"
need_not_grep 'Entitity' "$WORK/log.csv" "csv header no longer misspells Entity"

rm -f "$WORK/log.csv"

run_esmini --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log --csv_logger mycsv.csv
need_status 0 $? "esmini --csv_logger <filename> (explicit form)"
need_file "$WORK/mycsv.csv" "explicit csv_logger output file"
[ ! -e "$WORK/log.csv" ] || fail "explicit --csv_logger filename must not also create the default log.csv"
exit 0
