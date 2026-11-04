#!/usr/bin/env bash
# The control for the light-state query: a vehicle in a scenario that contains
# no LightStateAction at all. Every one of the fourteen lamp types must read
# back as "unknown" for the whole run -- not off, not on, and not an error.
#
# This is the assertion a build that reports a plausible-looking default
# (everything off, or everything emitting) gets wrong, and it is asked of the
# second entity so the query is not answered from the Ego alone.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_light_state "$WORK/two_car_brake.xosc" 1
need_status 0 $? "se_light_state on an entity with no light actions"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# The driver samples inside a one-step-wide window around each whole second, so
# it samples roughly twice per second; 10 is the collapsed-run floor, not a pin
# on how many step boundaries land in the window.
samples=$(sed -n 's/^emitted_samples=//p' "$WORK/last.out" | tail -1)
[ -n "$samples" ] && [ "$samples" -ge 10 ] \
    || fail "expected at least 10 sampled instants, got '${samples:-none}'"

# Every sampled line is a successful query that reports unknown / not emitting.
total=$(grep -c '^t[0-9.]* [a-z_]* rc=' "$WORK/last.out")
unknown=$(grep -c '^t[0-9.]* [a-z_]* rc=0 mode=3 emit=0$' "$WORK/last.out")
[ "$total" -eq "$unknown" ] \
    || fail "expected every lamp sample to be rc=0 mode=3 emit=0, got $unknown of $total"
[ "$total" -eq $((samples * 14)) ] \
    || fail "expected 14 lamp types per sample, got $total lines for $samples samples"

# Named spot checks, so the count above cannot be satisfied by an empty run.
for lamp in brake ind_left ind_right fog lowbeam highbeam tail warning \
            daytime fogfront fogrear reversing license special; do
    need_grep "^t0\.00 $lamp rc=0 mode=3 emit=0$" "$WORK/last.out" "$lamp at t=0"
done

need_kv badobj -1 "unknown object id"
