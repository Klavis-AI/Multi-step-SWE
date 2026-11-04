#!/usr/bin/env bash
# The struct-based forms, SE_GetVariable and SE_SetVariable. They address the
# same store the typed calls do: a value written with SE_SetVariableInt is
# visible through SE_GetVariable, and a value written through SE_SetVariable is
# visible through SE_GetVariableInt.
#
# Only the integer, double and boolean variants are exercised. For a string
# variable upstream reinterprets the struct's void* as a pointer to a C++
# std::string rather than to a C string; that is not a stable C ABI and this
# step does not grade it.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" struct || fail "se_variables struct exited $?"
need_kv init 0 "SE_Init on the variable fixture"

need_kv struct_get_int_rc 0 "SE_GetVariable on an integer variable"
need_kv struct_get_int 7 "value SE_GetVariable wrote through the struct"
need_kv struct_set_int_rc 0 "SE_SetVariable on an integer variable"
need_kv struct_int_after 101 "SE_GetVariableInt sees what SE_SetVariable wrote"

need_kv struct_get_double_rc 0 "SE_GetVariable on a double variable"
need_close struct_get_double 2.75 1e-9 "value SE_GetVariable wrote through the struct"
need_kv struct_set_double_rc 0 "SE_SetVariable on a double variable"
need_close struct_double_after -3.5 1e-9 "SE_GetVariableDouble sees what SE_SetVariable wrote"

need_kv struct_get_bool_rc 0 "SE_GetVariable on a boolean variable"
need_kv struct_get_bool 1 "value SE_GetVariable wrote through the struct"
# The read went through a `bool *` and must have written one byte through it,
# not four: three canary bytes sit immediately behind the flag in the driver.
need_kv struct_bool_canary 1 "SE_GetVariable wrote a boolean at bool width"
need_kv struct_set_bool_rc 0 "SE_SetVariable on a boolean variable"
need_kv struct_bool_after 0 "SE_GetVariableBool sees what SE_SetVariable wrote"

# The two families are one namespace, not two.
need_kv pre_seed_rc 0 "SE_SetVariableInt return code"
need_kv struct_sees_typed_rc 0 "SE_GetVariable after a typed write"
need_kv struct_sees_typed 7654 "SE_GetVariable reports what SE_SetVariableInt wrote"

# An undeclared name is an error in both directions, and must not crash.
need_kv struct_get_absent_rc -1 "SE_GetVariable on an undeclared name"
need_kv struct_set_absent_rc -1 "SE_SetVariable on an undeclared name"

need_kv survived 1 "driver ran to the end"
