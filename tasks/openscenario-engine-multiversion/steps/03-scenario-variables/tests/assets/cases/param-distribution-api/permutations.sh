#!/usr/bin/env bash
# The parameter value distribution API an earlier step established. Two
# distribution files of different sizes are loaded in separate processes and
# the reported count must follow the file.
source /tests/caselib.sh

cp "$FIXTURES/param_sweep.xosc" "$FIXTURES/straight_500m.xodr" \
   "$FIXTURES/dist_six.xosc" "$FIXTURES/dist_three.xosc" "$WORK/"

run_driver se_param_dist "$WORK/dist_six.xosc" "$WORK/param_sweep.xosc" 0 \
    || fail "se_param_dist exited $?"
need_kv perms_before 0 "SE_GetNumberOfPermutations before any distribution is set"
need_kv set_dist 0 "SE_SetParameterDistribution"
need_kv perms 6 "SE_GetNumberOfPermutations for a 3x2 distribution"
need_kv perms_after_reset 0 "SE_GetNumberOfPermutations after SE_ResetParameterDistribution"
need_kv survived 1 "driver ran to the end"

run_driver se_param_dist "$WORK/dist_three.xosc" "$WORK/param_sweep.xosc" 0 \
    || fail "se_param_dist exited $?"
need_kv perms 3 "SE_GetNumberOfPermutations for a 3-value distribution"
need_kv perms_after_reset 0 "SE_GetNumberOfPermutations after SE_ResetParameterDistribution"
