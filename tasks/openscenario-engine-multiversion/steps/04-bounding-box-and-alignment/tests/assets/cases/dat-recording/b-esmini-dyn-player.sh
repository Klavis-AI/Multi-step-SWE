#!/usr/bin/env bash
# INHERITED. WORKSPACE_HANDOFF.md's "What you receive" also says the tree
# "plays back a recording through `esmini-dyn`". esmini-dyn is the
# shared-library-driven player: it does the same job as the `esmini`
# executable but through libesminiLib.so, so it is the one artifact that fails
# if the library's published surface stops loading. It does not start at all
# unless libesminiLib.so is on the loader path, which is why caselib's
# run_tool sets LD_LIBRARY_PATH for every product invocation and not just for
# the drivers.
#
# Measured identical at v2.27.5 and v2.31.10 on this fixture.
source /tests/caselib.sh

cp "$FIXTURES/two_car_brake.xosc" "$FIXTURES/straight_500m.xodr" "$WORK/"

run_tool "$ESMINI_DYN" --osc "$WORK/two_car_brake.xosc" --headless --fixed_timestep 0.05
need_status 0 $? "esmini-dyn headless run"
need_grep 'esmini BUILD VERSION: ' "$WORK/last.out" \
    "esmini-dyn reports the same build banner the library publishes"
need_grep '^Closing$' "$WORK/last.out" "esmini-dyn shut down cleanly through the library"

# `Closing` on its own proves nothing: esmini-dyn prints it -- and still exits
# 0 -- even when the scenario failed to load. Measured, not assumed. So the
# case also requires the evidence of a run that really happened, and the
# absence of the two failure lines a bad load produces.
need_not_grep 'Failed to load'                "$WORK/last.out" "the scenario loaded"
need_not_grep 'Failed to initialize scenario' "$WORK/last.out" "the player initialised"
need_grep '^0\.000: Init Ego TeleportAction standbyState -> startTransition -> runningState$' \
    "$WORK/last.out" "the storyboard's Init actions ran through the library"
need_grep '^0\.000: Pos\(20\.00, -1\.53, 0\.00\)' "$WORK/last.out" \
    "and placed Ego where the scenario says, same as the static player does"
