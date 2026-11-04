#!/usr/bin/env bash
# Regression floor. Two long-standing embedding-ABI features that have nothing
# to do with the new geometry and collision queries, and must keep working:
#
#   * parameter-value distributions -- load a distribution file, ask how many
#     permutations it enumerates, select one by index, and read the index back
#     after the scenario has been initialised;
#   * initialising from a scenario held in memory as a string rather than from
#     a path on disk.
#
# The distribution fixture crosses three Ego speeds with two Target positions,
# so the permutation count is a product the engine computes, not a constant the
# fixture states. Measured identical at both ends of this step.
source /tests/caselib.sh

cp "$FIXTURES/speed_dist.xosc" "$FIXTURES/speed_param.xosc" \
   "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_perm_initstr "$WORK/speed_dist.xosc" "$WORK/speed_param.xosc" 3
need_status 0 $? "se_perm_initstr with permutation index 3"
need_kv n_perm_before 0 "no permutations are known before a distribution is loaded"
need_kv load 0 "loading the distribution file"
need_kv n_perm 6 "three speeds crossed with two positions"
# Sign only, not a magnitude: step 02's instruction fixes the rejection's sign
# ("must instead be rejected with a negative return ... Which negative value is
# returned is not specified and is not graded; only the sign is"), and the step
# that introduced this entry point is the only place the contract is written.
# Pinning -1 here would grade the one thing that document excludes. It is still
# a real assertion: a reader that clamps an out-of-range index to a valid one
# answers 0 or a small code and fails, which is what the check is for.
need_neg select_oob "selecting an index one past the last permutation is refused"
need_kv select 0 "selecting a valid permutation index"
# The value SE_GetPermutationIndex reports BEFORE SE_Init is deliberately NOT
# asserted. Step 02's instruction states the opposite reading for the release
# that introduced the call ("A successful load leaves permutation 0 selected,
# so the index reads 0 immediately after SE_SetParameterDistribution returns
# and before any explicit SE_SelectPermutation") and step 02's own scored case
# reads the selected index back before SE_Init; the reference moves to "the
# index is not reported until SE_Init applies the selection" inside step 03's
# range and no document of any step states that move. Asserting either reading
# here would fail one of the two conforming trees by construction, so what is
# graded is the index after SE_Init (below) and the pre-init read is left out.
need_kv init 0 "SE_Init of the selected permutation"
need_kv index_after_init 3 "the selected index is reported back after initialisation"
need_kv n_perm_after_reset 0 "the permutation set is cleared by the reset"
need_kv initstr 0 "SE_InitWithString on the same scenario held in memory"
need_kv initstr_ego_speed 10.0000 "the in-memory scenario's declared Ego speed"
need_kv survived 1 "SE_Close"

# A different index must be reported back as that index, not as the first one.
run_driver se_perm_initstr "$WORK/speed_dist.xosc" "$WORK/speed_param.xosc" 5
need_status 0 $? "se_perm_initstr with permutation index 5"
need_kv n_perm 6 "permutation count is independent of the index selected"
need_kv select 0 "selecting the last permutation index"
need_kv init 0 "SE_Init of the last permutation"
need_kv index_after_init 5 "the last index is reported back"
need_kv initstr 0 "SE_InitWithString after the last permutation"
need_kv survived 1 "SE_Close after the last permutation"
