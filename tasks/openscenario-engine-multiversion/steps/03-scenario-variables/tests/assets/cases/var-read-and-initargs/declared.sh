#!/usr/bin/env bash
# The four typed variable readers, against the values the fixture declares in
# its <VariableDeclarations> block. Seven variables of four different types are
# read in one process, so a reader that returns a single constant answers at
# most one of them.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" read || fail "se_variables read exited $?"
need_kv init 0 "SE_Init on the variable fixture"

# integer, both signs
need_kv cruise_rc 0 "SE_GetVariableInt return code"
need_kv cruise 7 "SE_GetVariableInt(CruiseCount)"
need_kv segment_rc 0 "SE_GetVariableInt return code on a negative value"
need_kv segment -13 "SE_GetVariableInt(SegmentIndex)"

# double
need_kv headway_rc 0 "SE_GetVariableDouble return code"
need_close headway 2.75 1e-9 "SE_GetVariableDouble(HeadwayTarget)"

# string
need_kv manoeuvre_rc 0 "SE_GetVariableString return code"
need_kv manoeuvre overtake_left "SE_GetVariableString(ManoeuvreTag)"
need_kv phase_rc 0 "SE_GetVariableString return code"
need_kv phase phase_initial "SE_GetVariableString(PhaseTag)"

# boolean, both values
need_kv assist_on_rc 0 "SE_GetVariableBool return code"
need_kv assist_on 1 "SE_GetVariableBool(AssistEnabled) declared true"
need_kv assist_armed_rc 0 "SE_GetVariableBool return code"
need_kv assist_armed 0 "SE_GetVariableBool(AssistArmed) declared false"

need_kv survived 1 "driver ran to the end"
