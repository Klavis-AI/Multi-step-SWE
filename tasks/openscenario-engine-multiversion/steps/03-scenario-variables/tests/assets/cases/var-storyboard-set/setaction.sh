#!/usr/bin/env bash
# A variable the storyboard assigns while the scenario runs is visible through
# the variable readers. The fixture's global VariableAction fires at 2 s, so
# the same entry point returns two different strings depending on when it is
# asked -- which no constant can reproduce.
source /tests/caselib.sh

cp "$FIXTURES/variables_demo.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_variables "$WORK/variables_demo.xosc" story || fail "se_variables story exited $?"
need_kv init 0 "SE_Init on the variable fixture"

need_kv phase_t0_rc 0 "SE_GetVariableString before stepping"
need_kv phase_t0 phase_initial "PhaseTag holds its declared value at t=0"

need_close time_a 1.0 0.05 "simulation time after 10 steps of 0.1 s"
need_kv phase_t1 phase_initial "PhaseTag is unchanged before the action's start trigger"

need_close time_b 4.0 0.05 "simulation time after 40 steps of 0.1 s"
need_kv phase_t4_rc 0 "SE_GetVariableString after the action ran"
need_kv phase_t4 phase_running "PhaseTag holds the value the VariableAction assigned"

# The action names one variable and changes one variable.
need_kv cruise_end 7 "an unrelated integer variable is untouched"
need_kv manoeuvre_end overtake_left "an unrelated string variable is untouched"

need_kv survived 1 "driver ran to the end"
