#!/usr/bin/env bash
# The align flag is the whole point of this entry point: it decides whether the
# handle's existing heading survives the placement. Everything else about the
# resulting position must be identical either way, so a implementation that
# ignores the flag, or that applies it to the coordinates as well, is caught.
source /tests/caselib.sh

ODR="$FIXTURES/straight_500m.xodr"

# align = 1 -> heading is overwritten with the road direction. straight_500m
# runs along +x and lane -1 faces along it, so the aligned heading is 0.
run_driver rm_road_pos "$ODR" 1 250 -1.535 1
need_status 0 $? "rm_road_pos with align=1"
cp "$WORK/last.out" "$WORK/aligned.out"
need_close h 0.0000 0.0001 "heading after aligned placement"
need_kv heading_preserved 0 "aligned placement replaced the heading"

# align = 0 -> the heading the handle already carried survives. The driver sets
# a heading of 1 rad before placing, so 1 rad is what must come back.
run_driver rm_road_pos "$ODR" 1 250 -1.535 0
need_status 0 $? "rm_road_pos with align=0"
cp "$WORK/last.out" "$WORK/unaligned.out"
need_close h 1.0000 0.0001 "heading after unaligned placement"
need_kv heading_preserved 1 "unaligned placement kept the heading"

# The two placements must agree on everything except the heading. This is what
# separates a real alignment mode from a flag that perturbs the position too.
for k in road lane s x y lane_offset; do
    a=$(sed -n "s/^$k=//p" "$WORK/aligned.out" | tail -1)
    b=$(sed -n "s/^$k=//p" "$WORK/unaligned.out" | tail -1)
    [ -n "$a" ] || fail "key $k missing from the aligned run"
    [ "$a" = "$b" ] || fail "align flag changed $k: aligned=$a unaligned=$b"
done
exit 0
