#!/usr/bin/env bash
# A distribution document that declares no parameters at all is accepted and
# enumerates nothing. The engine must report zero rather than one, so a caller
# looping over permutations runs the scenario exactly once from another path.
source /tests/caselib.sh

run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_none.xosc" 0 || fail "se_param_dist exited $?"
need_kv load 0 "SE_SetParameterDistribution on a document with no parameter distribution"
need_kv n_perm 0 "an empty distribution enumerates no permutations"
need_kv index_loaded 0 "the index stays at zero when nothing is enumerated"
# There is no valid index into an empty enumeration, not even 0: selecting it
# is refused rather than silently accepted as a single implicit permutation.
need_neg select "selecting index 0 against zero enumerated permutations"
need_kv survived 1 "the engine survives an empty distribution"
