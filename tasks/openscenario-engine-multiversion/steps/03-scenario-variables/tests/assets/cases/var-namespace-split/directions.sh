#!/usr/bin/env bash
# Variables and parameters are two namespaces, not one store with two sets of
# accessors. The fixture declares two parameters and seven variables and no
# name appears in both blocks, so every cross lookup below must fail while the
# matching one succeeds.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" split || fail "se_variables split exited $?"
need_kv init 0 "SE_Init on the variable fixture"

# Parameter accessors must not reach a variable.
need_kv param_reads_variable_rc -1 "SE_GetParameterDouble on a declared variable"
need_kv param_reads_variable_str_rc -1 "SE_GetParameterString on a declared variable"
need_kv param_writes_variable_rc -1 "SE_SetParameterDouble on a declared variable"

# Variable accessors must not reach a parameter.
need_kv variable_reads_param_rc -1 "SE_GetVariableDouble on a declared parameter"
need_kv variable_reads_param_str_rc -1 "SE_GetVariableString on a declared parameter"
need_kv variable_writes_param_rc -1 "SE_SetVariableDouble on a declared parameter"

# Each namespace still answers for its own names, with the declared values, so
# the failures above are separation rather than the accessors being broken.
need_kv param_reads_param_rc 0 "SE_GetParameterDouble on a declared parameter"
need_close param_reads_param 18.5 1e-9 "declared parameter value"
need_kv param_reads_param_str_rc 0 "SE_GetParameterString on a declared parameter"
need_kv param_reads_param_str parameter_side "declared parameter value"
need_kv variable_reads_variable_rc 0 "SE_GetVariableDouble on a declared variable"
need_close variable_reads_variable 2.75 1e-9 "declared variable value"
need_kv variable_reads_variable_str_rc 0 "SE_GetVariableString on a declared variable"
need_kv variable_reads_variable_str overtake_left "declared variable value"

need_kv survived 1 "driver ran to the end"
