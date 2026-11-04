#!/usr/bin/env bash
# The readers report failure rather than inventing a value: for a name nothing
# declares, for a declared name asked through the reader for the wrong type,
# and before any scenario has been loaded at all.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" read || fail "se_variables read exited $?"

# No scenario loaded yet: there is no variable namespace to answer from.
need_kv preinit_rc -1 "SE_GetVariableInt before SE_Init"
need_kv preinit_set_rc -1 "SE_SetVariableInt before SE_Init"

# Initialisation returns 0 only on success: a scenario file that does not exist
# is reported as -1 rather than accepted. The command-line half of the same
# rule is asserted in esmini-cli-headless/run.sh.
need_kv init_bad -1 "SE_Init on a scenario file that does not exist"

need_kv init 0 "SE_Init on the variable fixture"

# A declared variable through the reader for a different type.
need_kv cruise_as_double_rc -1 "SE_GetVariableDouble on an integer variable"
need_kv headway_as_int_rc -1 "SE_GetVariableInt on a double variable"
need_kv assist_as_string_rc -1 "SE_GetVariableString on a boolean variable"
need_kv manoeuvre_as_bool_rc -1 "SE_GetVariableBool on a string variable"

# A name that appears in no declaration block.
need_kv absent_int_rc -1 "SE_GetVariableInt on an undeclared name"
need_kv absent_double_rc -1 "SE_GetVariableDouble on an undeclared name"
need_kv absent_string_rc -1 "SE_GetVariableString on an undeclared name"
need_kv absent_bool_rc -1 "SE_GetVariableBool on an undeclared name"

need_kv survived 1 "driver ran to the end"
