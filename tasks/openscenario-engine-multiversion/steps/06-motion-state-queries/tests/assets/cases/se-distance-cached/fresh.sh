#!/usr/bin/env bash
# The cached distance query with a tracking limit the pair never exceeds inside
# the run: every sample is recomputed, the timestamp tracks simulation time,
# and the answer agrees with the uncached road-manager query.
source /tests/caselib.sh

run_driver se_simple_dist "$FIXTURES/two_car_brake.xosc" 300 0.01 200.0 4 || fail "se_simple_dist exited $?"
need_kv survived 1 "driver ran to completion"

# The two vehicles close from 100 m to 70 m over 3 s.
need_kv rc 0 "final call succeeded"
need_close distance 70.0 0.01 "distance after 3 s"
need_close stamp 3.0 0.001 "timestamp equals simulation time while fresh"
need_close stamp_lag 0.0 0.001 "no lag while fresh"
need_kv fresh_calls 300 "every call recomputed"
need_kv stale_calls 0 "no call went stale"
need_kv stamp_moves 300 "the timestamp advanced on every call"

# The cached answer equals the uncached one, to the tolerance the library uses.
need_kv legacy_rc 0 "SE_GetDistanceToObject succeeded"
need_close legacy_ds 70.0 0.01 "uncached distance after 3 s"
need_kv agrees 1 "cached and uncached agree"

need_neg rc_bad "unknown object id reports failure"
