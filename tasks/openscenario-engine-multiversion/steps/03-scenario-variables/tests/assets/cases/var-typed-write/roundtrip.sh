#!/usr/bin/env bash
# The four typed variable writers. Every writer is used twice with two
# different values and the matching reader is asked after each one, so the
# value that comes back is the value that went in rather than a constant or
# the value the file declared.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" write || fail "se_variables write exited $?"
need_kv init 0 "SE_Init on the variable fixture"

need_kv set_int_a_rc 0 "SE_SetVariableInt return code"
need_kv cruise_a 42 "SE_GetVariableInt after SE_SetVariableInt(42)"
need_kv set_int_b_rc 0 "SE_SetVariableInt return code"
need_kv cruise_b -900 "SE_GetVariableInt after SE_SetVariableInt(-900)"

need_kv set_double_a_rc 0 "SE_SetVariableDouble return code"
need_close headway_a 0.125 1e-9 "SE_GetVariableDouble after SE_SetVariableDouble(0.125)"
need_kv set_double_b_rc 0 "SE_SetVariableDouble return code"
need_close headway_b -987.654321 1e-9 "SE_GetVariableDouble after SE_SetVariableDouble(-987.654321)"

need_kv set_string_a_rc 0 "SE_SetVariableString return code"
need_kv manoeuvre_a hold_lane "SE_GetVariableString after SE_SetVariableString(hold_lane)"
need_kv set_string_b_rc 0 "SE_SetVariableString return code"
need_kv manoeuvre_b merge_right "SE_GetVariableString after SE_SetVariableString(merge_right)"

# Both boolean directions: one declared true is set false, one declared false
# is set true.
need_kv set_bool_a_rc 0 "SE_SetVariableBool return code"
need_kv assist_on_a 0 "SE_GetVariableBool after SE_SetVariableBool(false)"
need_kv set_bool_b_rc 0 "SE_SetVariableBool return code"
need_kv assist_armed_b 1 "SE_GetVariableBool after SE_SetVariableBool(true)"

need_kv survived 1 "driver ran to the end"
