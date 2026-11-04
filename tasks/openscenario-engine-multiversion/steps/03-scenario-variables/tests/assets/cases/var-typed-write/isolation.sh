#!/usr/bin/env bash
# A write lands on exactly one variable. It does not touch its neighbours, it
# does not change the variable's declared type, and it does not bring an
# undeclared name into existence.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" write || fail "se_variables write exited $?"
need_kv init 0 "SE_Init on the variable fixture"

# Untouched variables still hold their declared values after eight writes to
# other names.
need_kv segment_after -13 "SegmentIndex is unchanged by writes to other variables"
need_kv phase_after phase_initial "PhaseTag is unchanged by writes to other variables"

# Replacing an integer variable's value leaves it an integer variable.
need_kv cruise_as_double_after_rc -1 "SE_GetVariableDouble on an integer variable after a write"

# Writing an undeclared name fails and does not create the variable.
need_kv absent_set_int_rc -1 "SE_SetVariableInt on an undeclared name"
need_kv absent_set_double_rc -1 "SE_SetVariableDouble on an undeclared name"
need_kv absent_set_string_rc -1 "SE_SetVariableString on an undeclared name"
need_kv absent_set_bool_rc -1 "SE_SetVariableBool on an undeclared name"
need_kv absent_after_set_rc -1 "the undeclared name is still undeclared"

need_kv survived 1 "driver ran to the end"
