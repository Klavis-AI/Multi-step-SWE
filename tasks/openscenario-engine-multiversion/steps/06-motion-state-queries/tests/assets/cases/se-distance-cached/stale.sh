#!/usr/bin/env bash
# The same pair with a tracking limit of 30 m. The vehicles start 100 m apart
# and only close to 80 m over this run, so they never come within the limit: the
# first sample is computed and every later one is served from the cache, which
# the call has to admit through its return code and its timestamp.
#
# The run is 200 steps of 0.01 s deliberately, not 300. Requirement 5 says the
# stale sample is held for three seconds of simulation time before the query
# recomputes; the sample is taken at t = 0.01, so a 300-step run ends with the
# hold at 2.99 s - one timestep inside the boundary, which would make all three
# of the exact counts below flip together on any implementation that measured
# the hold slightly differently. At 200 steps the hold ends at 1.99 s, a full
# second clear of the boundary, and nothing this case tests is lost: it is still
# one computed sample and every later call served from the cache.
source /tests/caselib.sh

run_driver se_simple_dist "$FIXTURES/two_car_brake.xosc" 200 0.01 30.0 4 || fail "se_simple_dist exited $?"
need_kv survived 1 "driver ran to completion"

# The negative return is the documented "this sample is stale" answer, and it
# is what separates this entry point from the uncached query.
need_kv rc -2 "final call reports a stale sample"
need_kv fresh_calls 1 "only the first sample was computed"
need_kv stale_calls 199 "every later sample came from the cache"
need_kv stamp_moves 1 "the timestamp never advanced past the first sample"

# The cached distance is frozen at the value of the first sample (100 m, the
# separation at t=0.01) rather than following the vehicles in to 80 m.
need_close distance 99.9 0.01 "distance frozen at the first sample"
need_close stamp 0.01 0.001 "timestamp frozen at the first sample"
need_close stamp_lag 1.99 0.01 "timestamp lags simulation time"
