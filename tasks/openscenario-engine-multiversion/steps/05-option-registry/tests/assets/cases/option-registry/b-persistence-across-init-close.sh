#!/usr/bin/env bash
# New. Options set through the plain (non-persistent) entry points are
# dropped by a scenario run, but options set through the "...Persistent"
# entry points survive an SE_Init/SE_Close cycle within the same process.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_options elev_cruise.xosc
need_status 0 $? "d_options"

need_kv ephem_before 1 "ephemeral option reads as set before the scenario runs"
need_kv init1_ok 1 "the reset cycle's SE_Init loaded the real scenario"
need_kv ephem_after 0 "a plain SE_SetOption flag is cleared by SE_Init/SE_Close"
need_kv ephemval_after 0 "a plain SE_SetOptionValue value is cleared by SE_Init/SE_Close"
need_kv init2_ok 1 "the second reset cycle's SE_Init loaded the real scenario"
need_kv keep_after 1 "SE_SetOptionPersistent survives SE_Init/SE_Close"
need_kv keepval_after 99 "SE_SetOptionValuePersistent survives SE_Init/SE_Close, with its value intact"

# R05.21's lifecycle half, on the RM_ side. The RM_ persistent pair was set
# before both cycles in the driver ran, so these lines are the RM_ mirror of the
# two above. The value question is asked as a SET FLAG rather than through
# RM_GetOptionValue: on the built v2.44.2 tree that getter answers "v1" for
# every name once it has been read once -- `static std::string val = ...` inside
# the lib, whose initialiser runs for the life of the process -- so a value
# assertion there would be measuring the lib's cache, not the store. The
# value-side evidence is carried by `rm_shared_se_val` in case a (the same
# store, read back through the SE_ getter, which re-fills its buffer per call)
# and by `rm_keepval_after` here: a persistent value that the cycle dropped
# would also clear the flag.
need_kv rm_keep_after 1 "RM_SetOptionPersistent survives SE_Init/SE_Close"
need_kv rm_keepval_after 1 "RM_SetOptionValuePersistent survives SE_Init/SE_Close"
need_kv rm_shared_se_keep 1 "an RM_ persistent option is visible through SE_GetOptionSet after the cycles"
# ...and the stored STRING itself, read back through the SE_ getter rather than
# the RM_ one for the cache reason above. R2 states the two stores are one
# store, so this is a legitimate read of what RM_SetOptionValuePersistent wrote.
# It closes the last inferred link in the RM_ persistence chain: without it a
# build that kept the persistent NAME and dropped its VALUE passes every other
# line in this file.
need_kv rm_keepval_after_val 77 "RM_SetOptionValuePersistent stored the value, not just the flag"

# What is deliberately NOT asserted: the non-persistent RM_ counterpart, and the
# valued-option form of RM_UnsetOption. Both are measured and neither behaves as
# the SE_ side does at this release: `RM_UnsetOption` acts "on a pure option,
# without arguments" only (CommonMini.cpp's own guard), so a valued option stays
# set; and a plain RM_ value that is never unset but is re-read after the cycles
# comes back `(null)` while the flag it was set with comes back `1`. Those are
# the release's own behaviours at both tags, not capabilities this step
# introduces, so asserting either would grade the candidate on something the
# instruction never asks them to change. Recorded in REVIEW_LOG.md.
need_kv survived 1 "driver completed"
exit 0
