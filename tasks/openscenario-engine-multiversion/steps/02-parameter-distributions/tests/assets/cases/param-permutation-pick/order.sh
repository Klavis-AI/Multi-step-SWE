#!/usr/bin/env bash
# Which assignment a permutation index selects. The order is part of the
# contract: the last declared parameter varies fastest, and the values within a
# parameter keep declaration order.
#
# Six permutations over EgoSpeed {12, 24} and TargetSpeed {5, 10, 15} must give
# six distinct pairs in a fixed sequence, so both the index mapping and the
# enumeration order are checked at once.
source /tests/caselib.sh

for i in 0 1 2 3 4 5; do
  run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" "$i" || fail "se_param_dist $i exited $?"
  need_kv select 0 "selecting permutation $i"
  need_kv index_selected "$i" "reported index after selecting $i"
  need_kv init 0 "scenario init under permutation $i"
  # SE_Init must not reset the selection to the first permutation, and must not
  # discard the distribution: both are re-read on the far side of it.
  need_kv n_perm_after_init 6 "the distribution survives SE_Init under permutation $i"
  need_kv index_after_init "$i" "the selection survives SE_Init under permutation $i"
done

# Each index resolves to its own assignment, observable in the running scenario
# rather than only in the mixer: the target is placed at a different station and
# runs at a different speed for each index.
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" 0 || fail "se_param_dist 0 exited $?"
need_close target_speed 5 1e-3 "target speed under permutation 0"
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" 1 || fail "se_param_dist 1 exited $?"
need_close target_speed 10 1e-3 "target speed under permutation 1"
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" 3 || fail "se_param_dist 3 exited $?"
need_close target_speed 5 1e-3 "target speed under permutation 3, where the first parameter steps"
need_close ego_speed 24 1e-3 "ego speed under permutation 3"
