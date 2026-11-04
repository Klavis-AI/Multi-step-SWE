#!/usr/bin/env bash
# Out-of-range and negative permutation indices are rejected without disturbing
# the distribution that is loaded, and the scenario still runs afterwards.
source /tests/caselib.sh

run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_set6.xosc" 2 || fail "se_param_dist exited $?"
need_neg select_oob "selecting the permutation count itself is out of range"
need_neg select_negative "a negative permutation index is refused"
need_kv n_perm 6 "the loaded distribution enumerates six permutations"
need_kv select 0 "a valid selection still succeeds after a refused one"
need_kv index_selected 2 "the reported index follows the valid selection"

# The three keys above are all read BEFORE any refusal reaches a distribution
# that has a selection standing, so on their own they cannot tell a refusal
# apart from a clamp. The driver therefore refuses both indices a second time,
# with permutation 2 already selected, and re-reads the count and the index
# afterwards. This is where "rejected rather than clamped" is actually decided:
# a clamping implementation returns 0 here, or leaves the index at 0 or 5.
need_neg select_oob_after "the permutation count is still out of range once a selection stands"
need_neg select_negative_after "a negative index is still refused once a selection stands"
need_kv n_perm_after_refusal 6 "the enumeration survives a refused selection"
need_kv index_after_refusal 2 "a refused selection leaves the standing selection untouched"

need_kv init 0 "the scenario still initialises"
need_kv survived 1 "the engine survives refused selections"
