#!/usr/bin/env bash
# Regression floor. Reading OpenSCENARIO VariableDeclarations through the
# embedding ABI: the declared value comes back, the type is enforced, an
# undeclared name is refused, and a scenario that declares no variables at all
# refuses every read rather than inventing a value. Measured identical at both
# ends of this step.
source /tests/caselib.sh

cp "$FIXTURES/speed_param.xosc" "$FIXTURES/two_car_brake.xosc" \
   "$FIXTURES/straight_500m.xodr" "$WORK/"

# speed_param.xosc declares Counter (int, 7) and BrakeRequest (boolean, false).
run_driver se_variables "$WORK/speed_param.xosc"
need_status 0 $? "se_variables on a scenario with declarations"
need_kv init 0 "SE_Init"
need_grep '^counter_rc=0 counter_val=7$' "$WORK/last.out" "the declared integer variable"
need_grep '^brake_rc=0 brake_val=0$' "$WORK/last.out" "the declared boolean variable"
need_kv counter_as_double_rc -1 "reading an integer variable as a double is refused"
need_kv unknown_rc -1 "reading an undeclared variable is refused"
need_kv survived 1 "SE_Close"

# two_car_brake.xosc has no VariableDeclarations at all. Every read must be
# refused, and the caller's storage must be left alone -- the driver seeds the
# boolean with true before the call, so brake_val=1 shows it was not written.
run_driver se_variables "$WORK/two_car_brake.xosc"
need_status 0 $? "se_variables on a scenario with no declarations"
need_kv init 0 "SE_Init on a scenario with no declarations"
need_grep '^counter_rc=-1 counter_val=-1$' "$WORK/last.out" "integer read refused, storage untouched"
need_grep '^brake_rc=-1 brake_val=1$' "$WORK/last.out" "boolean read refused, storage untouched"
need_kv counter_as_double_rc -1 "double read refused"
need_kv unknown_rc -1 "undeclared name refused"
need_kv survived 1 "SE_Close on a scenario with no declarations"
