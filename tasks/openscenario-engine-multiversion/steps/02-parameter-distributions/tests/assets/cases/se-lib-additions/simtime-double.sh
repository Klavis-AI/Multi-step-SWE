#!/usr/bin/env bash
# The simulation clock is readable at double precision, and the double reading
# is not the single-precision reading widened back out.
#
# The instant is chosen so that claim is decidable for EVERY correct
# implementation, not just for the one upstream happens to have. The step size
# reaches the engine as a `float`, and (float)0.1 is exactly 13421773 * 2^-27.
# Twenty-one of those summed in double precision is exactly 281857233 * 2^-27:
# every partial sum has a numerator below 2^53, so the accumulation is exact
# whatever order a double clock adds them in. 281857233 is ODD, so that value
# is not a multiple of 2^-22 -- which is the float spacing on [2,4) -- and is
# therefore not representable as a float, for any conforming implementation.
# A genuine double clock must differ from its own float narrowing here; a
# double accessor that merely widens the float reading back out cannot.
# (Measured at v2.27.0: 2.1000000312924385 against 2.1000001430511475.)
#
# The instant matters. At a float-exact one such as 2.0 s the two readings
# legitimately coincide and this check would fail a correct solution.
#
# Neither reading is pinned to an exact string. Both are compared numerically:
# the instruction requires only that they agree to within float resolution.
source /tests/caselib.sh

run_driver se_reports "$FIXTURES/param_cars.xosc" "$WORK/rec.dat" 21 0.1 \
    || fail "se_reports exited $?"

# The three zero readings are stated, not assumed: instruction.md §5 says the
# clock starts at zero, that both accessors read 0 before any SE_Init, and that
# they read 0 again on a scenario that has been initialised but not stepped.
need_kv time_before 0 "the clock before any scenario is initialised"
need_kv init 0 "the fixture scenario initialises"
need_kv time_init_double 0 "the clock of a scenario that has not advanced"
need_kv time_init_float 0 "both precisions agree at zero"

need_close time_double 2.1 1e-6 "the clock after 21 steps of 0.1 s"
need_close time_float  2.1 1e-6 "the single-precision clock agrees to within float resolution"

# The distinguishing claim: at this instant the two readings are not the same
# number. A double reader that just widens the float loses exactly this.
td=$(sed -n 's/^time_double=//p' "$WORK/last.out" | tail -1)
tf=$(sed -n 's/^time_float=//p'  "$WORK/last.out" | tail -1)
[ -n "$td" ] || fail "no time_double reading"
[ "$td" = "$tf" ] && fail "the double-precision clock reported $td, identical to the single-precision reading; it must carry the precision the float one loses"
exit 0
