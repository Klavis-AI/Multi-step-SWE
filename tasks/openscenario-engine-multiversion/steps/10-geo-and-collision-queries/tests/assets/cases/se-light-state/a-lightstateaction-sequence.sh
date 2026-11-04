#!/usr/bin/env bash
# LightStateAction drives per-entity lamp state, and SE_GetObjectLightState
# reads it back. light_sequence.xosc switches four different lamps at four
# different times on ONE of its two vehicles:
#
#   t > 1 s  brake lights   -> on
#   t > 2 s  left indicator -> on
#   t > 3 s  fog lights     -> off
#   t > 3 s  high beam      -> flashing
#
# so one run has to give five different answers -- on, off, flashing,
# untouched, and "changed a second later" -- for lamps of the same vehicle.
# light_mode is documented as 0 off / 1 on / 2 flashing / 3 unknown; every
# value below was read off the running engine.
#
# The scenario's second vehicle, "Bystander", is named by no Actors element,
# so it is the control for instruction requirement 3's "affects only the
# entity the manoeuvre's actor names": it is in the same scenario, on the same
# road, while every one of those actions fires, and must come out untouched.
#
# The driver samples once per simulation step inside a one-step-wide window
# around each whole AND each half second, so a run yields four sample sets per
# simulation second: N.00, N.05, N.50 and N.55. An action gated on "t > N" is
# not visible at the t = N sample and is visible by the t = N+1 sample -- as
# measured, the brake lamp is still unknown at t = 1.05 and on at t = 2.00 --
# and the sample times below allow for the whole interval in between rather
# than assuming any particular number of steps of latency.
source /tests/caselib.sh

cp "$FIXTURES/light_sequence.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_driver se_light_state "$WORK/light_sequence.xosc" 0
need_status 0 $? "se_light_state"
need_kv init 0 "SE_Init"
need_kv survived 1 "SE_Close"

# The driver reached the end of the storyboard rather than dying early. The
# threshold is 10, not 12: the sampling window admits two sample sets per
# simulation second, so a full six-second run emits about twelve, and the
# assertion is here to catch a run that collapsed to nothing -- not to pin how
# many times a 0.05 s step lands inside a one-step-wide window, which no
# document fixes and the engine's float accumulation decides.
samples=$(sed -n 's/^emitted_samples=//p' "$WORK/last.out" | tail -1)
[ -n "$samples" ] && [ "$samples" -ge 10 ] \
    || fail "expected at least 10 sampled instants, got '${samples:-none}'"

# Before any LightStateAction has fired, every one of the fourteen lamp types
# reads back as unknown and not emitting.
for t in 0.00 1.00; do
    n=$(grep -c "^t$t [a-z_]* rc=0 mode=3 emit=0$" "$WORK/last.out")
    [ "$n" -eq 14 ] || fail "at t=$t expected all 14 lamps unknown/not emitting, got $n"
done

# Brake lights go on first and stay on.
need_grep '^t1\.00 brake rc=0 mode=3 emit=0$' "$WORK/last.out" "brake lights before the action"
for t in 2.00 3.00 4.00 5.00; do
    need_grep "^t$t brake rc=0 mode=1 emit=1$" "$WORK/last.out" "brake lights on at t=$t"
done

# The left indicator goes on one second later, and the right one never does.
need_grep '^t2\.00 ind_left rc=0 mode=3 emit=0$' "$WORK/last.out" "left indicator before the action"
for t in 3.00 4.00 5.00; do
    need_grep "^t$t ind_left rc=0 mode=1 emit=1$" "$WORK/last.out" "left indicator on at t=$t"
done
need_grep '^t5\.00 ind_right rc=0 mode=3 emit=0$' "$WORK/last.out" "right indicator never touched"

# Switching the fog lights OFF is not the same as leaving them alone: mode 0 is
# a state the engine was told to enter, mode 3 is the state it was never told
# anything about. The generic fogLights entry drives the front and rear ones too.
for lamp in fog fogfront fogrear; do
    need_grep "^t3\.00 $lamp rc=0 mode=3 emit=0$" "$WORK/last.out" "$lamp before the action"
    need_grep "^t4\.00 $lamp rc=0 mode=0 emit=0$" "$WORK/last.out" "$lamp switched off at t=4"
    need_grep "^t5\.00 $lamp rc=0 mode=0 emit=0$" "$WORK/last.out" "$lamp still off at t=5"
done

# At the last sample the three groups really are in three different states.
need_grep '^t5\.00 brake rc=0 mode=1 emit=1$' "$WORK/last.out" "final state: brake on"
need_grep '^t5\.00 fog rc=0 mode=0 emit=0$' "$WORK/last.out" "final state: fog off"
need_grep '^t5\.00 lowbeam rc=0 mode=3 emit=0$' "$WORK/last.out" "final state: low beam unknown"

# Flashing is mode 2, and mode 2 is a steady reading: the mode does not blink
# even though the lamp does. `emit` is deliberately NOT pinned at any single
# instant -- the instruction says it follows the flash phase, so it is a
# property of the instant sampled and not of the lamp, and a case that pinned
# one sample time would be asserting this fixture's flash alignment.
need_grep '^t3\.00 highbeam rc=0 mode=3 emit=0$' "$WORK/last.out" "high beam before the action"
for t in 4.00 5.00; do
    need_grep "^t$t highbeam rc=0 mode=2 emit=[01]$" "$WORK/last.out" "high beam flashing at t=$t"
done

# What the instruction DOES promise about a flashing lamp is that `emitting`
# alternates, so that is asserted as the property it is rather than as a pin on
# one instant. The fixture's cycle is 0.5 s on / 0.5 s off -- a 1.0 s period --
# and the driver samples at whole and at half seconds, which are half a period
# apart, so the two offsets cannot land in the same phase whichever instant the
# cycle happens to start from. Both readings must therefore appear among the
# mode-2 samples. This catches "always emitting" and "never emitting" alike,
# neither of which alternates, and neither of which a single pinned sample time
# would separate from a correct implementation whose phase differs from this
# build's.
hb_on=$(grep -c '^t[0-9.]* highbeam rc=0 mode=2 emit=1$' "$WORK/last.out")
hb_off=$(grep -c '^t[0-9.]* highbeam rc=0 mode=2 emit=0$' "$WORK/last.out")
[ "$hb_on" -ge 1 ] && [ "$hb_off" -ge 1 ] \
    || fail "flashing high beam did not alternate: $hb_on emitting and $hb_off not-emitting mode-2 samples"

# An unknown object is the documented failure.
need_kv badobj -1 "unknown object id"

# Entity isolation. Same scenario, same run length, the other vehicle -- the
# one no Actors element names. Every lamp of it must still read unknown while
# the actor's brake light, indicator, fog lamps and high beam have all moved.
run_driver se_light_state "$WORK/light_sequence.xosc" 1
need_status 0 $? "se_light_state on the bystander"
need_kv init 0 "SE_Init for the bystander pass"
need_kv survived 1 "SE_Close for the bystander pass"

bsamples=$(sed -n 's/^emitted_samples=//p' "$WORK/last.out" | tail -1)
[ -n "$bsamples" ] && [ "$bsamples" -ge 10 ] \
    || fail "bystander pass: expected at least 10 samples, got '${bsamples:-none}'"
btotal=$(grep -c '^t[0-9.]* [a-z_]* rc=' "$WORK/last.out")
buntouched=$(grep -c '^t[0-9.]* [a-z_]* rc=0 mode=3 emit=0$' "$WORK/last.out")
[ "$btotal" -eq $((bsamples * 14)) ] \
    || fail "bystander pass: expected 14 lamp types per sample, got $btotal for $bsamples samples"
[ "$btotal" -eq "$buntouched" ] \
    || fail "LightStateAction leaked onto an entity its actor never named: $buntouched of $btotal lamp samples unknown"

# Named spot checks on exactly the lamps the actor's actions moved, at the last
# whole second, so the counts above cannot be satisfied by an empty run.
for lamp in brake ind_left fog fogfront fogrear highbeam; do
    need_grep "^t5\.00 $lamp rc=0 mode=3 emit=0$" "$WORK/last.out" \
        "bystander's $lamp untouched at t=5"
done
