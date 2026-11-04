#!/usr/bin/env bash
# MERGED CASE. This file is two cases that used to live side by side in this
# group, align-none-and-soft-preserve.sh and align-hard-snaps-to-road.sh,
# folded into one file to stay inside the package's per-step case budget.
# Nothing was dropped: every assertion and every comment from both halves is
# below, in its original order. Where a comment says a thing is "checked
# separately", it now means the other half of this same file.
# test.sh.in scores the GROUP, not the case file: `score` is the share-weighted
# fraction of the SCORED groups that passed, and the case count never enters
# that arithmetic -- so merging two files into one changes no weight and no
# denominator. What it does cost is localisation. A failure in either half now
# fails the whole file and therefore the whole group, and groups.txt's count
# column counts .sh files on disk, so this file is ONE counted case carrying
# the assertions of two.
#
# ---------------------------------------------------------------------------
# Half 1 (was align-none-and-soft-preserve.sh)
# ---------------------------------------------------------------------------
# SE_SetAlignModeZ(ALIGN_NONE) and SE_SetAlignModeZ(ALIGN_SOFT) both leave a
# stationary object's elevation exactly where it was, in contrast to
# ALIGN_HARD (checked separately): neither mode forces a value onto an
# object that never asked to move, so a library that always snaps to the
# road regardless of mode, or that silently drops the caller's elevation
# even under NONE, is distinguishable from one that treats the modes
# correctly.
source /tests/caselib.sh

cp "$FIXTURES/align_crest.xosc" "$FIXTURES/crest-curve.xodr" "$WORK/"

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 0 z
need_kv init 0 "SE_Init"
need_close before_z 100.0000 0.01 "the wrong elevation took effect before alignment was touched"
need_close after_z 100.0000 0.01 "ALIGN_NONE leaves the elevation untouched"
need_kv survived 1 "driver ran to the end (mode 0)"

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 1 z
need_kv init 0 "SE_Init (second run)"
need_close before_z 100.0000 0.01 "the wrong elevation took effect again"
need_close after_z 100.0000 0.01 "ALIGN_SOFT preserves the object's existing offset when it has not moved"
need_kv survived 1 "driver ran to the end (mode 1)"

# ---------------------------------------------------------------------------
# The documented DEFAULT, for an object no align entry point has ever been
# called on: instruction.md §5 states it is ALIGN_SOFT for all four
# components. The driver's "missing" mode reaches it by reporting the object
# through SE_ReportObjectPosXYH -- the entry point with no z of its own --
# without touching SE_SetAlignMode or any of its per-component forms.
#
# This run starts from a freshly added object whose elevation has never been
# set, so ALIGN_SOFT has no offset to preserve and the object takes the road
# surface exactly as ALIGN_HARD does. The assertion is therefore that the
# default behaves like SOFT/HARD here and NOT like ALIGN_NONE: a build whose
# default is NONE would leave the object at 0, which is measurably not the
# road elevation at this point (3.0).
# ---------------------------------------------------------------------------

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 0 missing
need_status 0 $? "se_align, no align call at all"
need_kv init 0 "SE_Init (third run)"
need_kv add_id 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv report_rc 0 "the x/y/h-only report"
need_close after_z 3.0 0.05 "with no align call, the default follows the road like ALIGN_SOFT, not like ALIGN_NONE"
need_kv after_road 0 "the default left the object on its road"
need_kv after_lane -1 "the default left the object in its lane"
need_kv survived 1 "driver ran to the end (default)"

# ---------------------------------------------------------------------------
# Half 2 (was align-hard-snaps-to-road.sh)
# ---------------------------------------------------------------------------
# SE_SetAlignModeZ(ALIGN_HARD) forces an externally reported object's
# elevation to the road surface, overriding whatever elevation the object
# last held.
#
# The driver first reports a deliberately wrong elevation (100 m, on a road
# whose real surface there is nowhere near that high), confirms it stuck,
# then switches the object to hard z-alignment and re-reports the SAME x,y
# through the position entry point that carries no z of its own. Only a
# library that actually honours SE_SetAlignModeZ(ALIGN_HARD) replaces the
# stale value with the real road elevation at that point.

run_driver se_align "$WORK/align_crest.xosc" 229.1888 -27.9390 2 z
need_kv init 0 "SE_Init"
need_kv add_id 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv report_rc 0 "the initial full-pose report"
need_close before_z 100.0000 0.01 "the wrong elevation took effect before alignment was touched"
need_kv report2_rc 0 "the re-report after SE_SetAlignModeZ"
need_close after_z 3.0 0.05 "ALIGN_HARD replaced the stale elevation with the real road surface"
need_kv survived 1 "driver ran to the end"

# ---------------------------------------------------------------------------
# Half 3 (new at round 4): the same four behaviours on a SECOND network.
# ---------------------------------------------------------------------------
# Everything above runs on crest-curve.xodr, whose surface at the probe point
# is 3.0 m. A library that has not implemented z-alignment at all, but that
# happens to place externally reported objects at a small constant elevation,
# is not cleanly separated from a correct one by a single 3.0 m reading. So the
# same four runs are repeated on curves_elevation.xodr, where the surface at
# the probe point is 14.542 m -- a value no plausible constant produces, and
# far enough from both 3.0 and the deliberately wrong 100.0 that the three
# outcomes cannot be confused.
#
# The probe point (307.2716, 349.7025) is road 1, lane -1, s = 577.000 on that
# network; the elevation there is fixed by the road's elevation profile, so
# 14.542 is static geometry, not a simulated value.
#
# This is also the fixture pair WORKSPACE_HANDOFF.md's fixture list names and
# that R04.3 calls out by name. Before this half existed, both fixtures shipped in
# the workdir and no case read either of them.

cp "$FIXTURES/align_elevation.xosc" "$FIXTURES/curves_elevation.xodr" "$WORK/"

# ALIGN_NONE: the wrong elevation survives, exactly as on the first network.
run_driver se_align "$WORK/align_elevation.xosc" 307.2716 349.7025 0 z
need_kv init 0 "SE_Init (elevation network, mode 0)"
need_close before_z 100.0000 0.01 "the wrong elevation took effect before alignment was touched"
need_close after_z 100.0000 0.01 "ALIGN_NONE leaves the elevation untouched on the elevation network too"
need_kv survived 1 "driver ran to the end (elevation network, mode 0)"

# ALIGN_SOFT on an object that HAS an elevation of its own: the offset it is
# holding is preserved, so the reading does not move to the road surface.
run_driver se_align "$WORK/align_elevation.xosc" 307.2716 349.7025 1 z
need_kv init 0 "SE_Init (elevation network, mode 1)"
need_close before_z 100.0000 0.01 "the wrong elevation took effect again"
need_close after_z 100.0000 0.01 "ALIGN_SOFT preserves the existing offset on the elevation network too"
need_kv survived 1 "driver ran to the end (elevation network, mode 1)"

# ALIGN_HARD: the stale 100.0 is replaced by the real surface, 14.542 -- which
# is the assertion the second network exists to make sharp.
run_driver se_align "$WORK/align_elevation.xosc" 307.2716 349.7025 2 z
need_kv init 0 "SE_Init (elevation network, mode 2)"
need_kv add_id 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv report_rc 0 "the initial full-pose report"
need_close before_z 100.0000 0.01 "the wrong elevation took effect before alignment was touched"
need_kv report2_rc 0 "the re-report after SE_SetAlignModeZ"
need_close after_z 14.5420 0.01 "ALIGN_HARD replaced the stale elevation with this network's road surface"
need_kv survived 1 "driver ran to the end (elevation network, mode 2)"

# The default, on a freshly added object that has never held an elevation: no
# offset to preserve, so SOFT reports the road surface. Same reading as HARD,
# and nowhere near the 0 an ALIGN_NONE default would leave behind.
run_driver se_align "$WORK/align_elevation.xosc" 307.2716 349.7025 0 missing
need_status 0 $? "se_align on the elevation network, no align call at all"
need_kv init 0 "SE_Init (elevation network, default)"
need_kv add_id 1 "SE_AddObjectWithBoundingBox returned a usable id"
need_kv report_rc 0 "the x/y/h-only report"
need_close after_z 14.5420 0.01 "with no align call, the default follows this network's road like ALIGN_SOFT"
need_kv after_road 1 "the default left the object on road 1 of the elevation network"
need_kv after_lane -1 "the default left the object in lane -1"
need_close after_s 577.000 0.01 "the object landed at the s the probe point corresponds to"
need_kv survived 1 "driver ran to the end (elevation network, default)"
