#!/usr/bin/env bash
# Asking for the permutation count skips the scenario entirely. Recording and
# csv logging are both requested and neither may produce a file, because no
# permutation is ever executed.
source /tests/caselib.sh

cp "$FIXTURES/param_sweep.xosc" "$FIXTURES/straight_500m.xodr" "$FIXTURES/dist_six.xosc" "$WORK/"
cd "$WORK" || fail "cannot enter $WORK"

run_esmini --osc "$WORK/param_sweep.xosc" --param_dist "$WORK/dist_six.xosc" \
           --return_nr_permutations --headless --fixed_timestep 0.1 \
           --record "$WORK/rec.dat" --csv_logger "$WORK/out.csv"
need_status 6 $? "exit status for a six-permutation distribution"

# Upstream numbers per-permutation output files, so the check globs rather than
# looking for the exact names that were asked for.
produced=$(find "$WORK" -maxdepth 1 \( -name '*.dat' -o -name '*.csv' \) | sort | tr '\n' ' ')
[ -z "$produced" ] || fail "the scenario was executed: $produced"
exit 0
