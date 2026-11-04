#!/usr/bin/env bash
# Inherited. A catalog-free OpenSCENARIO file (a vehicle defined inline, no
# external catalog reference) must still load and report its object and
# initial position correctly -- through the C ABI driver, and through the
# shared-library-driven player esmini-dyn, which is one of the six delivered
# artifacts and is the only binary that exercises libesminiLib.so's public
# surface end to end from the command line.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_basic_scenario elev_cruise.xosc
need_status 0 $? "d_basic_scenario"

need_kv init_ret 0 "catalog-free scenario initializes"
need_kv nobjs 1 "one object in the scenario"
need_close x0 100.114 0.01 "initial world x position"
need_close y0 1.399 0.01 "initial world y position"
need_kv survived 1 "driver completed"

# esmini-dyn: the same catalog-free scenario must load and run to completion
# through the shared library. Only the exit status and the fact that it got
# past scenario loading are graded; the player's per-step output is its own.
#
# The two greps below are deliberately looser than the exact sentences the
# player prints. This step replaces the logging backend (instruction R6,
# WORKSPACE_HANDOFF "Logging backend") and re-formats every entry, so the
# exact wording of these two lines is NOT part of the inherited contract; a
# port is free to rename or restructure them. What is graded is the EVENT each
# line announces -- the scenario file and the road network were both loaded --
# read off a log line rather than off the process command line, which is what
# the trailing " (v1.0)" / end-of-line anchoring below rules out.
run_tool "$ESMINI_DYN" --osc elev_cruise.xosc --headless --fixed_timestep 0.05 --disable_log
need_status 0 $? "esmini-dyn runs the catalog-free scenario"
need_grep 'elev_cruise\.xosc \(v1\.0\)' "$WORK/last.out" "esmini-dyn reports loading the scenario"
need_grep 'curves_elevation\.xodr$' "$WORK/last.out" "esmini-dyn reports loading the road network"
exit 0
