#!/usr/bin/env bash
# The option store as the starting release already provides it: set a value,
# read it back, ask whether a flag is set, unset both. The new multi-value
# counting query is asserted separately in se-option-multi-value.
#
# One observable is deliberately NOT asserted here. SE_UnsetOption's effect on
# a previously stored *value* is not stable across this step: at v2.44.2 the
# value survives the unset, at v2.46.4 it is cleared. Requirement 13 covers
# SE_SetOptionValue, SE_GetOptionValue, SE_GetOptionSet and SE_SetOption only,
# and a gate group must hold at both ends of the step, so this case
# asserts the unset *return code* and the flag's cleared state - both of which
# are invariant - and leaves the value-clearing change to the scored groups.
source /tests/caselib.sh

run_driver se_option_store osc myfile.xosc headless || fail "se_option_store exited $?"
need_kv survived 1 "driver ran to completion"

# Nothing is set before the driver sets it, so a store that reported a stale
# value from a previous process would fail here.
need_kv value_before "(empty)" "option value is empty before it is set"
need_kv flag_before 0 "flag option is unset before it is set"

need_kv set_rc 0 "SE_SetOptionValue succeeded"
need_kv value_after myfile.xosc "SE_GetOptionValue returns what was stored"
need_kv flag_after_value_set 1 "an option with a value reads as set"

need_kv set_flag_rc 0 "SE_SetOption succeeded"
need_kv flag_after 1 "SE_GetOptionSet reports the flag as set"

need_kv unset_rc 0 "SE_UnsetOption succeeded on a value option"
need_kv unset_flag_rc 0 "SE_UnsetOption succeeded on a flag option"
need_kv flag_cleared 0 "SE_GetOptionSet reports the flag as unset again"

# Setting a name the registry does not know about is accepted rather than
# refused at both ends of this step. Asserting an error here would be wrong,
# and asserting success is what actually pins the behaviour.
need_kv set_unknown_rc 0 "an unregistered option name is accepted"
