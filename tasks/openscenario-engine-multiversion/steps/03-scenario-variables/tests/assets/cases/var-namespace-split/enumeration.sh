#!/usr/bin/env bash
# The parameter enumeration counts parameters only. The fixture declares two
# parameters alongside seven variables, so a build that parsed
# <VariableDeclarations> into the parameter store would report nine here and
# would name variables among the parameters.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" split || fail "se_variables split exited $?"
need_kv init 0 "SE_Init on the variable fixture"

need_kv nparams 2 "SE_GetNumberOfParameters with 2 parameters and 7 variables declared"
need_kv variable_names_in_parameter_list 0 "no variable name is returned by SE_GetParameterName"

# Writing a variable through the variable API does not graft it onto the
# parameter namespace either.
need_kv set_variable_rc 0 "SE_SetVariableDouble return code"
need_kv param_reads_variable_after_set_rc -1 "SE_GetParameterDouble on a variable that was just written"
need_kv nparams_after_set 2 "SE_GetNumberOfParameters after a variable write"
need_close variable_after_set 9.5 1e-9 "the write did land in the variable namespace"

need_kv survived 1 "driver ran to the end"
