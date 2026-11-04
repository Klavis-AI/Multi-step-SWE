#!/usr/bin/env bash
# Selecting a permutation changes the parameter values the scenario is built
# with, and the entity really drives at the selected speed. Three different
# indices of the same distribution give three different answers.
source /tests/caselib.sh

cp "$FIXTURES/param_sweep.xosc" "$FIXTURES/straight_500m.xodr" "$FIXTURES/dist_six.xosc" "$WORK/"

check() {  # check <index> <ego speed> <target speed>
    run_driver se_param_dist "$WORK/dist_six.xosc" "$WORK/param_sweep.xosc" "$1" \
        || fail "se_param_dist index $1 exited $?"
    need_kv select 0 "SE_SelectPermutation($1)"
    need_kv init 0 "SE_Init"
    need_kv index "$1" "SE_GetPermutationIndex after selecting $1"
    need_kv ego_speed_rc 0 "SE_GetParameterDouble(EgoSpeed)"
    need_close ego_speed "$2" 1e-6 "EgoSpeed resolved for permutation $1"
    need_close target_speed "$3" 1e-6 "TargetSpeed resolved for permutation $1"
    need_close sim_time 2.0 0.01 "SE_GetSimulationTimeDouble after 20 steps of 0.1 s"
    need_kv state 1 "SE_GetObjectState"
    need_close measured_speed "$2" 0.05 "the entity drives at the resolved speed"
    need_kv ego_id 0 "SE_GetIdByName(Ego)"
    need_kv absent_id_negative 1 "SE_GetIdByName on an unknown name is negative"
    need_kv survived 1 "driver ran to the end"
}

check 0 14.0 8.0
check 2 22.0 8.0
check 5 30.0 12.0
