#!/usr/bin/env bash
# --save_xosc writes out the scenario the player actually ran: the declared
# parameter values replaced by the ones the selected permutation assigns, under
# a filename that names the permutation. The parameter the distribution says
# nothing about keeps its declared value, so the substitution is targeted and
# not a rewrite of the whole declaration block.
source /tests/caselib.sh

cp "$FIXTURES/param_cars.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_set6.xosc" "$WORK/"

# --save_xosc takes no argument, so it must parse correctly anywhere in the
# command line, not only in the position where the next token happens to be
# absent. It is placed mid-line here on purpose: an implementation that
# consumed the following token as a filename would swallow --param_dist and
# produce no decorated output at all, which is exactly the failure this
# placement catches and a trailing --save_xosc cannot.
run_esmini --osc param_cars.xosc --headless --save_xosc \
           --fixed_timestep 0.05 --param_dist dist_set6.xosc --param_permutation 3
need_status 0 $? "esmini with --save_xosc given ahead of other options"

out=$WORK/param_cars_4_of_6.xosc
need_file "$out"

need_grep 'name="EgoSpeed".*value="24"'      "$out" "EgoSpeed populated from the selected permutation"
need_grep 'name="TargetSpeed".*value="5"'    "$out" "TargetSpeed populated from the selected permutation"
need_grep 'name="TargetStartS".*value="200"' "$out" "a parameter outside the distribution keeps its declared value"
need_not_grep 'name="EgoSpeed".*value="20"'  "$out" "the declared EgoSpeed must be replaced, not kept"

# The scenario the run was given is input, not output: it must come back
# untouched.
need_grep 'name="EgoSpeed".*value="20"' "$WORK/param_cars.xosc" \
    "the source scenario must not be rewritten in place"

# The strongest claim req 4 makes: what comes out is a scenario, not just a
# report about one. Feed it straight back to the player. A resolved file that
# dropped its ParameterDeclarations block, or wrote values that no longer
# parse, greps identically to a good one and only fails here: the engine exits
# 255 on a file it cannot load, so the exit status alone decides this claim.
#
# Deliberately not asserted here: what the re-run PRINTS. Both the load line
# and the resolved-value line are engine console text, and instruction.md's
# "Out of scope" list declares parameter logging ungraded -- so scoring either
# one would fail a solution that implemented §4 in full and kept §4's file
# contract without keeping the print. The resolved values themselves are
# asserted above, on the file the flag produced.
run_esmini --osc param_cars_4_of_6.xosc --headless --fixed_timestep 0.05
need_status 0 $? "the engine re-loading the file --save_xosc produced"

# The sweep form of §3 has to decorate saved scenarios the same way it
# decorates recordings: one resolved file per permutation, all six from one
# invocation, none of them under the undecorated name. Run in its own
# directory so the six files here cannot be confused with the single file the
# permutation-selected run above produced.
mkdir -p "$WORK/sweep"
cp "$FIXTURES/param_cars.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_set6.xosc" "$WORK/sweep/"
cd "$WORK/sweep" || fail "could not enter the sweep scratch directory"

run_esmini --osc param_cars.xosc --headless --fixed_timestep 0.05 \
           --save_xosc --param_dist dist_set6.xosc
need_status 0 $? "esmini saving a resolved scenario for every permutation"

for i in 1 2 3 4 5 6; do
    need_file "$WORK/sweep/param_cars_${i}_of_6.xosc" \
        "the resolved scenario for permutation $i of the sweep"
done

# The two ends of the enumeration carry the two ends of the value sequence,
# so the six files are six different resolutions rather than six copies.
need_grep 'name="EgoSpeed"[^>]*value="12"' "$WORK/sweep/param_cars_1_of_6.xosc" \
    "the sweep's first permutation resolves EgoSpeed to the first declared value"
need_grep 'name="TargetSpeed"[^>]*value="5"' "$WORK/sweep/param_cars_1_of_6.xosc" \
    "the sweep's first permutation resolves TargetSpeed to the first declared value"
need_grep 'name="EgoSpeed"[^>]*value="24"' "$WORK/sweep/param_cars_6_of_6.xosc" \
    "the sweep's last permutation resolves EgoSpeed to the last declared value"
need_grep 'name="TargetSpeed"[^>]*value="15"' "$WORK/sweep/param_cars_6_of_6.xosc" \
    "the sweep's last permutation resolves TargetSpeed to the last declared value"

# --save_xosc must also be DISCOVERABLE. instruction.md's "Exact output that must
# not change" states that the flag of §4 joins esmini's usage banner -- one option
# per line, indented by exactly two spaces -- rather than replacing what is there.
# Only the flag name and the indent are matched; the wording that follows it is
# upstream text no document pins. The banner's exit status is deliberately not
# checked, per the same section.
run_esmini --help
need_grep '^  --save_xosc' "$WORK/last.out" "--save_xosc is listed in the usage banner"
need_grep '^  --fixed_timestep' "$WORK/last.out" \
    "the inherited options are still listed beside it"
