#!/usr/bin/env bash
# Instruction requirement 7: an entity with no Route that drives into a
# junction leaves it on the connecting road that continues straight ahead, and
# does so the same way on every run.
#
# fabriksgatan.xodr is the verifier's own copy of a four-way crossing
# (junction id 4) fed by roads 0, 1, 2 and 3. junction_crossing.xosc puts one
# car on road 0 and one on road 3, neither with a route, and lets both drive
# through. "Straight ahead" on a four-way crossing is the road opposite the
# one the entity came in on, so road 0 must lead to road 2 and road 3 to
# road 1 -- and each entity must touch exactly three roads on the way
# (incoming, one connecting road inside the junction, outgoing).
#
# The connecting road id itself is NOT asserted against a constant: that is
# network topology the instruction never states. What is asserted is that all
# three runs agree on it exactly, which is the determinism half of the
# requirement and needs no constant at all.
source /tests/caselib.sh

cp "$FIXTURES/junction_crossing.xosc" "$FIXTURES/fabriksgatan.xodr" "$WORK/"

for run in 1 2 3; do
    run_driver se_junction "$WORK/junction_crossing.xosc" 400 0.05
    need_status 0 $? "se_junction run $run"
    cp "$WORK/last.out" "$WORK/run$run.out"

    need_kv init 0 "SE_Init on junction_crossing, run $run"
    need_kv objects 2 "both entities loaded, run $run"
    need_kv survived 1 "SE_Close, run $run"

    # Straight ahead across a four-way crossing: 0 -> 2 and 3 -> 1.
    need_kv obj0_final_road 2 "run $run: the car from road 0 leaves on road 2"
    need_kv obj1_final_road 1 "run $run: the car from road 3 leaves on road 1"

    # Exactly one connecting road between the incoming and the outgoing one:
    # the entity crossed the junction rather than wandering around inside it.
    need_kv obj0_hops 3 "run $run: obj0 crossed exactly one junction"
    need_kv obj1_hops 3 "run $run: obj1 crossed exactly one junction"
done

# Determinism: the same scenario, the same time step, the same route through
# the junction every time -- including the connecting road, which this
# compares rather than pins.
for run in 2 3; do
    a=$(grep -E '^obj[01]_path=' "$WORK/run1.out")
    b=$(grep -E '^obj[01]_path=' "$WORK/run$run.out")
    [ -n "$a" ] || fail "run 1 printed no path lines"
    [ "$a" = "$b" ] || fail "junction choice is not repeatable: run 1 gave '$a', run $run gave '$b'"
done
