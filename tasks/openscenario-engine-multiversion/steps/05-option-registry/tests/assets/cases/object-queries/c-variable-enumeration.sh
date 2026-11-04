#!/usr/bin/env bash
# New. Variables declared in the loaded scenario must be enumerable by index,
# each with its own name and type. three_vars.xosc declares three variables of
# mixed type. R4 says the enumeration ORDER is the engine's own and is not
# guaranteed to match declaration order, so this case grades the NAME SET
# (the driver sorts the names it enumerated) and the type attached to each
# name -- never the position a name came out at. The per-index varnameN /
# vartypeN lines the driver also prints are for a human reading the log and
# are deliberately not asserted.
source /tests/caselib.sh

cp "$FIXTURES/three_vars.xosc" "$WORK/three_vars.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_object_query three_vars.xosc
need_status 0 $? "d_object_query"

# R4: with no scenario loaded the count is negative. The driver probes this
# before its first SE_Init, so it is the "never loaded" state, and only the
# SIGN is graded -- R4 promises a negative value, not a particular one.
need_kv nvars_no_scenario_neg 1 "SE_GetNumberOfVariables is negative before a scenario is loaded"
need_kv nvars 3 "three variables declared in the scenario"
need_kv nvars_named 3 "every index in [0, nvars) returned a non-null name"
need_kv varnames_sorted firstVar,secondVar,thirdVar "the enumerated names are exactly the three declared (order-independent)"
need_kv vartype_of_firstVar 2 "firstVar is declared double (type code 2)"
need_kv vartype_of_secondVar 1 "secondVar is declared integer (type code 1)"
need_kv vartype_of_thirdVar 2 "thirdVar is declared double (type code 2)"
need_kv survived 1 "driver completed"
exit 0
