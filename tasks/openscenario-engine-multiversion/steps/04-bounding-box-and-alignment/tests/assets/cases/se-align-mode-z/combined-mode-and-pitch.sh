#!/usr/bin/env bash
# MERGED CASE. This file is two cases that used to live side by side in this
# group, combined-mode-covers-z.sh and pitch-follows-road.sh, folded into one
# file to stay inside the package's per-step case budget. Nothing was dropped:
# every assertion and every comment from both halves is below, in its original
# order.
# test.sh.in scores the GROUP, not the case file: `score` is the share-weighted
# fraction of the SCORED groups that passed, and the case count never enters
# that arithmetic -- so merging two files into one changes no weight and no
# denominator. What it does cost is localisation. A failure in either half now
# fails the whole file and therefore the whole group, and groups.txt's count
# column counts .sh files on disk, so this file is ONE counted case carrying
# the assertions of two.
#
# ---------------------------------------------------------------------------
# Half 1 (was combined-mode-covers-z.sh)
# ---------------------------------------------------------------------------
# SE_SetAlignMode sets all four alignment axes (heading, pitch, roll,
# elevation) with a single call. This checks that its elevation effect
# matches SE_SetAlignModeZ's: ALIGN_NONE leaves a stationary object's stale
# elevation alone, ALIGN_HARD replaces it with the real road surface. A
# library that implements the four per-axis setters but forgets to wire the
# combined setter's z component (or vice versa) fails exactly one of the two
# elevation-mode cases in this group.
source /tests/caselib.sh

cp "$FIXTURES/align_crest.xosc" "$FIXTURES/crest-curve.xodr" "$WORK/"

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 0 all
need_kv init 0 "SE_Init"
need_close before_z 100.0000 0.01 "the wrong elevation took effect before alignment was touched"
need_close after_z 100.0000 0.01 "SE_SetAlignMode(ALIGN_NONE) leaves the elevation untouched"
need_kv survived 1 "driver ran to the end (mode 0)"

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 2 all
need_kv init 0 "SE_Init (second run)"
need_close before_z 100.0000 0.01 "the wrong elevation took effect again"
need_close after_z 3.0 0.05 "SE_SetAlignMode(ALIGN_HARD) replaces the stale elevation with the real road surface"
need_kv survived 1 "driver ran to the end (mode 2)"

# ---------------------------------------------------------------------------
# Half 2 (was pitch-follows-road.sh)
# ---------------------------------------------------------------------------
# The per-component setters really are per-component: SE_SetAlignModeP governs
# PITCH, on the same three-mode scale as the Z component, and a library that
# wires all four names to the same elevation behaviour fails here.
#
# Pitch does not need the two-step trick the Z cases use. An explicit pitch is
# blended against the road's own pitch on every report, so one call is enough:
# the driver sets the mode, then reports the full pose with pitch = 0 at a
# point on crest-curve.xodr where the road is measurably tilted.
#
# ALIGN_SOFT is deliberately not asserted numerically here. instruction.md
# describes SOFT as blending the supplied value with the road, and the exact
# blend is not a number any document states -- pinning it would grade an
# implementation detail. NONE and HARD are the two ends the instruction does
# define, and they are what this case grades.

# need_angle <key> <expected radians> <tol> <what>
#   Angles are compared modulo a full turn, exactly as instruction.md §5 says
#   the checker does: a build that reports the road pitch as -0.1049 and one
#   that reports it as 6.1783 have given the same answer, and neither spelling
#   is more correct than the other.
need_angle() {
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$4: driver printed no '$1=' line"
    awk -v g="$got" -v e="$2" -v t="$3" 'BEGIN {
        pi = atan2(0, -1); d = g - e
        d = d - 2 * pi * int(d / (2 * pi))
        if (d >  pi) d -= 2 * pi
        if (d < -pi) d += 2 * pi
        if (d < 0) d = -d
        exit (d <= t) ? 0 : 1
    }' || fail "$4: $1 expected $2 +/- $3 modulo a full turn, got $got"
}

# ALIGN_NONE (0): the pitch the caller supplied is what the object keeps, even
# though the road under it is not level.
run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 0 p
need_status 0 $? "se_align, pitch component, ALIGN_NONE"
need_kv init 0 "SE_Init"
need_kv add_id 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv report_rc 0 "the full-pose report under ALIGN_NONE"
need_angle after_p 0.0 0.001 "ALIGN_NONE kept the pitch the caller supplied"
need_kv after_road 0 "the object resolved onto the road either way"
need_kv after_lane -1 "and onto the lane either way"
need_kv survived 1 "driver ran to the end"

# ALIGN_HARD (2): the supplied pitch is discarded and the road's own tilt at
# that point takes its place, composed with the object's own reported heading.
# The probe sits at s = 235, on the CLIMBING side of the crest at s = 270: the
# road's slope there is positive, so its own pitch is negative under the
# engine's sign convention, and the composed value the engine reports is what
# is asserted below.
run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 2 p
need_status 0 $? "se_align, pitch component, ALIGN_HARD"
need_kv init 0 "SE_Init"
need_kv report_rc 0 "the full-pose report under ALIGN_HARD"
need_angle after_p -0.104885 0.005 "ALIGN_HARD replaced the supplied pitch with the road's own"
need_kv after_road 0 "ALIGN_HARD did not move the object off its road"
need_kv after_lane -1 "ALIGN_HARD did not move the object out of its lane"
need_kv survived 1 "driver ran to the end"
