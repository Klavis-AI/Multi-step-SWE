#!/usr/bin/env bash
# The player reports the size of a parameter value distribution through its
# exit status, without running the scenario. Three distributions of three
# different sizes are asked for, so the status is read from the file rather
# than from a constant.
source /tests/caselib.sh

cp "$FIXTURES/param_sweep.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_six.xosc" "$FIXTURES/dist_three.xosc" "$WORK/"
cd "$WORK" || fail "cannot enter $WORK"

# 3 EgoSpeed values x 2 TargetSpeed values.
run_esmini --osc "$WORK/param_sweep.xosc" --param_dist "$WORK/dist_six.xosc" \
           --return_nr_permutations --headless
need_status 6 $? "exit status for a six-permutation distribution"

# 3 EgoStart values.
run_esmini --osc "$WORK/param_sweep.xosc" --param_dist "$WORK/dist_three.xosc" \
           --return_nr_permutations --headless
need_status 3 $? "exit status for a three-permutation distribution"

# No distribution selected at all: nothing to permute. This is the one status
# in this case a build that ignored the option entirely could also produce -- it
# would run the scenario to its 4 s stop trigger and exit 0 as well -- so the
# invocation carries --record and --csv_logger and the count that follows is the
# negative control: reporting a count is not running, so nothing may be written.
run_esmini --osc "$WORK/param_sweep.xosc" --return_nr_permutations --headless \
           --fixed_timestep 0.1 --record "$WORK/none.dat" --csv_logger "$WORK/none.csv"
need_status 0 $? "exit status with no parameter distribution"
ran=$(find "$WORK" -maxdepth 1 \( -name '*.dat' -o -name '*.csv' \) | sort | tr '\n' ' ')
[ -z "$ran" ] || fail "the scenario was executed while reporting a count of 0: $ran"

# The documented error return. The option's own help text spells it -1, and a
# process exit status is one unsigned byte, so the shell sees 255. A
# distribution file that is not there is the first way to reach it...
run_esmini --osc "$WORK/param_sweep.xosc" --param_dist "$WORK/no_such_dist.xosc" \
           --return_nr_permutations --headless
need_status 255 $? "exit status for a --param_dist file that does not exist"

# ...and a file that exists but carries no parameter value distribution is the
# second. Neither may be reported as a permutation count.
printf 'not a parameter value distribution\n' > "$WORK/bad_dist.xosc"
run_esmini --osc "$WORK/param_sweep.xosc" --param_dist "$WORK/bad_dist.xosc" \
           --return_nr_permutations --headless
need_status 255 $? "exit status for an unparseable --param_dist file"

# The option is part of the player's published option list, in the words
# instruction.md §5 prints. The indent is not asserted: the documents publish
# the two lines, not the column they start in. The exit status of --help is
# not asserted either -- upstream returns non-zero for it at this release.
run_esmini --help
need_grep '^ *--return_nr_permutations *$' "$WORK/last.out" \
    "--return_nr_permutations appears in esmini --help"
need_grep '^ *Return number of permutations without executing the scenario' "$WORK/last.out" \
    "the option's published help text"
