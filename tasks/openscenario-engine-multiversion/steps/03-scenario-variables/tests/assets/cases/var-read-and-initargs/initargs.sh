#!/usr/bin/env bash
# The same typed readers, on a scenario started through SE_InitWithArgs instead
# of SE_Init. Each element of the argument vector is one argument: the path
# handed over below carries a doubled space and a trailing space, so a build
# that re-splits an element on ' ' turns one path into several fragments, never
# finds the file, and none of the reads below ever happen.
source /tests/caselib.sh

spaced="$WORK/two  spaces "
mkdir -p "$spaced"
cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$spaced/"

run_driver se_variables "$spaced/variables_demo.xosc" initargs \
    || fail "se_variables initargs exited $?"

need_kv initargs_rc 0 "SE_InitWithArgs on a scenario path containing repeated spaces"
need_kv init 0 "the scenario loaded"

# The variable namespace is the one the fixture declares, reached the same way
# as after SE_Init -- the entry point differs, the answers do not.
need_kv cruise 7 "SE_GetVariableInt(CruiseCount) after SE_InitWithArgs"
need_kv segment -13 "SE_GetVariableInt(SegmentIndex) after SE_InitWithArgs"
need_close headway 2.75 1e-9 "SE_GetVariableDouble(HeadwayTarget) after SE_InitWithArgs"
need_kv manoeuvre overtake_left "SE_GetVariableString(ManoeuvreTag) after SE_InitWithArgs"
need_kv assist_on 1 "SE_GetVariableBool(AssistEnabled) after SE_InitWithArgs"
need_kv assist_armed 0 "SE_GetVariableBool(AssistArmed) after SE_InitWithArgs"

need_kv survived 1 "driver ran to the end"
