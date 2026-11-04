#!/usr/bin/env bash
# SE_SetParameterDistribution reports whether a distribution file was accepted,
# and the permutations it describes are visible before any scenario runs.
#
# Three distribution files of different shapes are loaded through the same
# entry point, and each reports its own permutation count, so no single
# constant satisfies this case. The rejection paths are the sibling case
# refused.sh; everything here is expected to load.
source /tests/caselib.sh

run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" 0 || fail "se_param_dist exited $?"
need_kv n_perm_before 0 "SE_GetNumberOfPermutations before any distribution is loaded"
need_kv load 0 "SE_SetParameterDistribution on a valid distribution"
need_kv n_perm 6 "permutation count of a 2 x 3 deterministic set"
need_kv index_loaded 0 "permutation index before an explicit selection"
need_kv n_perm_after_reset 0 "SE_ResetParameterDistribution clears the distribution"

# A second distribution with a different shape must report a different count.
# Two single-parameter sets with two and three elements give six; one range
# with five steps gives five. A stub returning a constant fails one of them.
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_range5.xosc" 0 || fail "se_param_dist exited $?"
need_kv load 0 "SE_SetParameterDistribution on a range distribution"
need_kv n_perm 5 "permutation count of a five-step range"

# The third shape crosses the two deterministic forms: one
# DeterministicMultiParameterDistribution holding a single ParameterValueSet,
# times a DeterministicSingleParameterDistribution of two elements, is two
# permutations. A count that ignored either factor gives 1.
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_mixed.xosc" 0 || fail "se_param_dist exited $?"
need_kv load 0 "SE_SetParameterDistribution on a multi-parameter value set crossed with a single-parameter set"
need_kv n_perm 2 "permutation count of one value set times a two-element set"
